#include "test_framework.h"

#include <cstdint>
#include <memory>
#include <tuple>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "chat/constants.h"
#include "chat/service_interfaces.h"
#include "chat_flow.h"
#include "jsoncpp/json/json.h"

namespace {

using P1::ApplyInfo;
using P1::ChatDependencies;
using P1::ErrorCodes;
using P1::UserInfo;

int Ec(ErrorCodes code) { return static_cast<int>(code); }

// —— fake 依赖：内存实现，替代 MySQL / Redis ——
struct FakeUser {
    int32_t uid = 0;
    int32_t sex = 0;
    std::string name;
    std::string email;
    std::string nick;
    std::string desc;
    std::string icon;
    std::string back;
};

class FakeUserStore : public P1::IUserStore {
public:
    std::vector<FakeUser> users;
    std::vector<std::shared_ptr<ApplyInfo>> applyList;
    std::vector<std::shared_ptr<UserInfo>> friendList;
    int getUserByUidCalls = 0;
    int getApplyListCalls = 0;
    int getFriendListCalls = 0;

    // 好友申请 / 认证：可注入失败，并记录调用参数
    bool authFriendApplyOk = true;
    bool addFriendApplyOk = true;
    bool addFriendOk = true;
    int addFriendApplyCalls = 0;
    int32_t lastApplyFrom = -1;
    int32_t lastApplyTo = -1;
    std::vector<std::tuple<int32_t, int32_t, int32_t>> authFriendApplyArgs;
    std::vector<std::tuple<int32_t, int32_t, std::string>> addFriendArgs;

    int RegisterUser(const std::string&, const std::string&, const std::string&, const std::string&) override { return -1; }
    bool CheckEmail(const std::string&, const std::string&) override { return false; }
    bool UpdatePassword(const std::string&, const std::string&) override { return false; }
    bool CheckPassword(const std::string&, const std::string&, UserInfo&) override { return false; }

    bool GetApplyList(int32_t, std::vector<std::shared_ptr<ApplyInfo>>& out, int, int) override
    {
        ++getApplyListCalls;
        out = applyList;
        return !applyList.empty();
    }

    bool GetFriendList(int32_t, std::vector<std::shared_ptr<UserInfo>>& out) override
    {
        ++getFriendListCalls;
        out = friendList;
        return !friendList.empty();
    }

    bool AddFriendApply(int32_t from, int32_t to) override
    {
        ++addFriendApplyCalls;
        lastApplyFrom = from;
        lastApplyTo = to;
        return addFriendApplyOk;
    }

    bool AuthFriendApply(int32_t from, int32_t to, int32_t status) override
    {
        authFriendApplyArgs.emplace_back(from, to, status);
        return authFriendApplyOk;
    }

    bool AddFriend(int32_t from, int32_t to, const std::string& backName) override
    {
        addFriendArgs.emplace_back(from, to, backName);
        return addFriendOk;
    }

    std::shared_ptr<UserInfo> GetUserByUid(uint32_t uid) override
    {
        ++getUserByUidCalls;
        for (const auto& u : users) {
            if (static_cast<uint32_t>(u.uid) == uid) {
                auto info = std::make_shared<UserInfo>();
                info->uid = u.uid;
                info->sex = u.sex;
                info->name = u.name;
                info->email = u.email;
                info->nick = u.nick;
                info->desc = u.desc;
                info->icon = u.icon;
                info->back = u.back;
                return info;
            }
        }
        return nullptr;
    }
};

class FakeKvStore : public P1::IKeyValueStore {
public:
    std::unordered_map<std::string, std::string> data;
    int getCalls = 0;

    void Set(const std::string& key, const std::string& val) override { data[key] = val; }
    void SetEx(const std::string& key, const std::string& val, std::chrono::seconds) override { data[key] = val; }
    std::optional<std::string> Get(const std::string& key) override
    {
        ++getCalls;
        auto it = data.find(key);
        if (it == data.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    P1::KvCompareDeleteResult CompareAndDelete(
        const std::string& key, const std::string& expected) override
    {
        auto it = data.find(key);
        if (it == data.end()) {
            return P1::KvCompareDeleteResult::NotFound;
        }
        if (it->second != expected) {
            return P1::KvCompareDeleteResult::Mismatch;
        }
        data.erase(it);
        return P1::KvCompareDeleteResult::Deleted;
    }
    void HSet(const std::string&, const std::string&, const std::string&) override {}
    std::optional<std::string> HGet(const std::string&, const std::string&) override { return std::nullopt; }
    std::unordered_map<std::string, std::string> HGetAll(const std::string&) override { return {}; }
    std::optional<std::string> HIncrBy(const std::string&, const std::string&, int64_t) override { return std::nullopt; }
    bool Del(const std::string& key) override { return data.erase(key) > 0; }
};

struct Fixture {
    static constexpr int32_t kNoAction = -999;
    FakeUserStore userStore;
    FakeKvStore kvStore;

    ChatDependencies deps() { return ChatDependencies { &userStore, &kvStore }; }

    Json::Value SearchRequest(const std::string& uid)
    {
        Json::Value req;
        req["uid"] = uid;
        return req;
    }

    Json::Value LoginRequest(int32_t uid, const std::string& token)
    {
        Json::Value req;
        req["uid"] = uid;
        req["token"] = token;
        return req;
    }

    std::string TokenKey(int32_t uid) const
    {
        return std::string(P1::USER_TOKEN_PREFIX) + std::to_string(uid);
    }

    Json::Value AddFriendRequest(int32_t fromUid, int32_t toUid)
    {
        Json::Value req;
        req["from_uid"] = fromUid;
        req["to_uid"] = toUid;
        req["name"] = "applicant";
        req["desc"] = "please add me";
        req["remark_name"] = "remark";
        return req;
    }

    // action < 0 表示不带 action 字段（旧客户端）
    Json::Value AuthRequest(int32_t peerUid, int32_t selfUid, int32_t action = kNoAction)
    {
        Json::Value req;
        req["from_uid"] = peerUid;
        req["to_uid"] = selfUid;
        req["remark_name"] = "remark";
        if (action != kNoAction) {
            req["action"] = action;
        }
        return req;
    }

    Json::Value TextChatRequest(int32_t fromUid, int32_t toUid)
    {
        Json::Value req;
        req["from_uid"] = fromUid;
        req["to_uid"] = toUid;
        Json::Value msg;
        msg["msg_id"] = "m1";
        msg["content"] = "hello";
        req["text_array"].append(msg);
        return req;
    }

    static FakeUser MakeUser(int32_t uid, const std::string& name)
    {
        FakeUser u;
        u.uid = uid;
        u.sex = 1;
        u.name = name;
        u.email = name + "@test.com";
        u.nick = "nick_" + name;
        u.desc = "desc_" + name;
        u.icon = "icon_" + name;
        u.back = "back_" + name;
        return u;
    }
};

// —— 搜索用户：入参校验 ——
TESTFW_TEST(SearchUser_empty_uid_invalid)
{
    Fixture f;
    const Json::Value rsp = P1::ProcessSearchUser(f.SearchRequest(""), f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK_EQ(f.kvStore.getCalls, 0); // 入参非法时不应访问缓存/DB
    CHECK_EQ(f.userStore.getUserByUidCalls, 0);
}

TESTFW_TEST(SearchUser_non_digit_uid_invalid)
{
    Fixture f;
    for (const std::string bad : { "abc", "12a", "-1", "1 2", "1.5" }) {
        const Json::Value rsp = P1::ProcessSearchUser(f.SearchRequest(bad), f.deps());
        CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    }
    CHECK_EQ(f.userStore.getUserByUidCalls, 0);
}

// —— 搜索用户：命中 Redis 缓存 ——
TESTFW_TEST(SearchUser_hit_redis_cache)
{
    Fixture f;
    Json::Value cached;
    cached["uid"] = 1001;
    cached["name"] = "cached_user";
    cached["nick"] = "cached_nick";
    f.kvStore.data[std::string(P1::USER_INFO_PREFIX) + "1001"] = cached.toStyledString();

    const Json::Value rsp = P1::ProcessSearchUser(f.SearchRequest("1001"), f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(rsp["name"].asString(), std::string("cached_user"));
    CHECK_EQ(rsp["nick"].asString(), std::string("cached_nick"));
    CHECK_EQ(f.userStore.getUserByUidCalls, 0); // 命中缓存不再查 MySQL
}

TESTFW_TEST(SearchUser_cache_miss_falls_back_to_mysql)
{
    Fixture f;
    f.userStore.users.push_back(Fixture::MakeUser(1002, "db_user"));

    const Json::Value rsp = P1::ProcessSearchUser(f.SearchRequest("1002"), f.deps());
    CHECK_EQ(f.userStore.getUserByUidCalls, 1);
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(rsp["uid"].asInt(), 1002);
    CHECK_EQ(rsp["sex"].asInt(), 1);
    CHECK_EQ(rsp["name"].asString(), std::string("db_user"));
    CHECK_EQ(rsp["email"].asString(), std::string("db_user@test.com"));
    CHECK_EQ(rsp["nick"].asString(), std::string("nick_db_user"));
    CHECK_EQ(rsp["desc"].asString(), std::string("desc_db_user"));
    CHECK_EQ(rsp["icon"].asString(), std::string("icon_db_user"));
}

TESTFW_TEST(SearchUser_not_found_returns_uid_invalid)
{
    Fixture f;
    const Json::Value rsp = P1::ProcessSearchUser(f.SearchRequest("9999"), f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK_EQ(f.userStore.getUserByUidCalls, 1);
}

TESTFW_TEST(SearchUser_null_dependencies_is_safe)
{
    const Json::Value rsp = P1::ProcessSearchUser(Fixture().SearchRequest("1001"), ChatDependencies {});
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
}

// —— 加好友申请 ——
TESTFW_TEST(AddFriend_writes_apply_and_builds_notify)
{
    Fixture f;
    const P1::AddFriendPlan plan = P1::ProcessAddFriend(f.AddFriendRequest(1001, 1002), 1001, f.deps());
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(plan.fromUid, 1001);
    CHECK_EQ(plan.toUid, 1002);
    CHECK_EQ(f.userStore.addFriendApplyCalls, 1);
    CHECK_EQ(f.userStore.lastApplyFrom, 1001);
    CHECK_EQ(f.userStore.lastApplyTo, 1002);
    CHECK_EQ(plan.notify["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(plan.notify["name"].asString(), std::string("applicant"));
    CHECK_EQ(plan.notify["desc"].asString(), std::string("please add me"));
    CHECK_EQ(plan.notify["apply_uid"].asInt(), 1001);
}

TESTFW_TEST(AddFriend_rejects_spoofed_from_uid)
{
    Fixture f;
    const P1::AddFriendPlan plan = P1::ProcessAddFriend(
        f.AddFriendRequest(2002, 1002), 1001, f.deps());
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK_EQ(f.userStore.addFriendApplyCalls, 0);
    CHECK(plan.notify.empty());
}

TESTFW_TEST(AddFriend_rejects_missing_or_self_target)
{
    Fixture f;
    const P1::AddFriendPlan self = P1::ProcessAddFriend(
        f.AddFriendRequest(1001, 1001), 1001, f.deps());
    CHECK_EQ(self.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));

    Json::Value missing = f.AddFriendRequest(1001, 1002);
    missing.removeMember("to_uid");
    const P1::AddFriendPlan noTarget = P1::ProcessAddFriend(missing, 1001, f.deps());
    CHECK_EQ(noTarget.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK_EQ(f.userStore.addFriendApplyCalls, 0);
}

TESTFW_TEST(AddFriend_apply_write_failure_returns_rpc_failed)
{
    Fixture f;
    f.userStore.addFriendApplyOk = false;
    const P1::AddFriendPlan plan = P1::ProcessAddFriend(
        f.AddFriendRequest(1001, 1002), 1001, f.deps());
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::RPC_FAILED));
    CHECK_EQ(plan.toUid, 0); // 失败时不能继续走通知路由
    CHECK(plan.notify.empty());
}

// —— 认证动作解析 / 状态映射 ——
TESTFW_TEST(ResolveAuthAction_defaults_to_accept)
{
    Fixture f;
    CHECK_EQ(P1::ResolveAuthAction(f.AuthRequest(1, 2)), P1::FRIEND_ACTION_ACCEPT);
    CHECK_EQ(P1::ResolveAuthAction(f.AuthRequest(1, 2, 1)), P1::FRIEND_ACTION_ACCEPT);
    CHECK_EQ(P1::ResolveAuthAction(f.AuthRequest(1, 2, 2)), P1::FRIEND_ACTION_REJECT);
}

TESTFW_TEST(ResolveAuthAction_rejects_invalid_values)
{
    Fixture f;
    for (const int32_t bad : { 0, 3, -1, 100 }) {
        CHECK_EQ(P1::ResolveAuthAction(f.AuthRequest(1, 2, bad)), -1);
    }
}

TESTFW_TEST(ResolveAuthAction_handles_non_numeric_action_without_throwing)
{
    // 非数字 action 不能把异常抛出 handler（旧实现 asInt() 会抛，客户端收不到回包）
    Json::Value req;
    req["action"] = "1";                  // 字符串：旧实现 asInt() 直接抛异常
    CHECK_EQ(P1::ResolveAuthAction(req), -1);

    req["action"] = "accept";
    CHECK_EQ(P1::ResolveAuthAction(req), -1);

    req["action"] = true;                 // 布尔：旧实现会误判成 1（同意）
    CHECK_EQ(P1::ResolveAuthAction(req), -1);

    req["action"] = Json::Value();        // null
    CHECK_EQ(P1::ResolveAuthAction(req), -1);

    Json::Value nested;
    nested["a"] = 1;
    req["action"] = nested;               // object
    CHECK_EQ(P1::ResolveAuthAction(req), -1);
}

TESTFW_TEST(ResolveAuthAction_accepts_real_numbers)
{
    Json::Value req;
    req["action"] = 1.0;                  // JSON 实数形式的 1
    CHECK_EQ(P1::ResolveAuthAction(req), P1::FRIEND_ACTION_ACCEPT);
    req["action"] = 3.7;                  // 非整数实数 → 非法
    CHECK_EQ(P1::ResolveAuthAction(req), -1);
}

TESTFW_TEST(ApplyStatusForAction_maps_accept_and_reject)
{
    CHECK_EQ(P1::ApplyStatusForAction(P1::FRIEND_ACTION_ACCEPT), P1::APPLY_STATUS_ACCEPTED);
    CHECK_EQ(P1::ApplyStatusForAction(P1::FRIEND_ACTION_REJECT), P1::APPLY_STATUS_REJECTED);
}

// —— 认证结果通知 ——
TESTFW_TEST(BuildAuthResultNotify_accept_carries_profile)
{
    UserInfo self;
    self.uid = 1002;
    self.name = "me";
    self.nick = "n_me";
    self.desc = "d_me";
    self.icon = "i_me";
    self.sex = 1;

    const Json::Value notify = P1::BuildAuthResultNotify(1001, 1002, P1::FRIEND_ACTION_ACCEPT, &self);
    CHECK_EQ(notify["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(notify["action"].asInt(), P1::FRIEND_ACTION_ACCEPT);
    CHECK_EQ(notify["from_uid"].asInt(), 1001);   // 接收通知者（申请人）自己
    CHECK_EQ(notify["to_uid"].asInt(), 1002);
    CHECK_EQ(notify["name"].asString(), std::string("me"));
    CHECK_EQ(notify["nick"].asString(), std::string("n_me"));
    CHECK_EQ(notify["desc"].asString(), std::string("d_me"));
    CHECK_EQ(notify["icon"].asString(), std::string("i_me"));
    CHECK_EQ(notify["sex"].asInt(), 1);
}

TESTFW_TEST(BuildAuthResultNotify_reject_has_no_profile)
{
    const Json::Value notify = P1::BuildAuthResultNotify(1001, 1002, P1::FRIEND_ACTION_REJECT, nullptr);
    CHECK_EQ(notify["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(notify["action"].asInt(), P1::FRIEND_ACTION_REJECT);
    CHECK(!notify.isMember("name"));
    CHECK(!notify.isMember("icon"));
}

TESTFW_TEST(BuildAuthResultNotify_accept_without_profile_reports_error)
{
    const Json::Value notify = P1::BuildAuthResultNotify(1001, 1002, P1::FRIEND_ACTION_ACCEPT, nullptr);
    CHECK_EQ(notify["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK_EQ(notify["action"].asInt(), P1::FRIEND_ACTION_ACCEPT);
}

// —— 好友认证 ——
TESTFW_TEST(AuthFriend_rejects_processing_for_another_user)
{
    Fixture f;
    f.userStore.users.push_back(Fixture::MakeUser(2001, "peer"));
    f.userStore.users.push_back(Fixture::MakeUser(1001, "me"));
    f.userStore.users.push_back(Fixture::MakeUser(1002, "other"));

    // 当前会话是 1001，却试图替 1002 处理申请。
    const P1::AuthFriendPlan plan = P1::ProcessAuthFriend(
        f.AuthRequest(2001, 1002, 1), 1001, f.deps());
    CHECK(!plan.ok);
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK(f.userStore.authFriendApplyArgs.empty());
    CHECK(f.userStore.addFriendArgs.empty());
}

TESTFW_TEST(AuthFriend_invalid_action_returns_error_json)
{
    Fixture f;
    f.userStore.users.push_back(Fixture::MakeUser(2001, "peer"));
    f.userStore.users.push_back(Fixture::MakeUser(1001, "me"));

    const P1::AuthFriendPlan plan = P1::ProcessAuthFriend(f.AuthRequest(2001, 1001, 3), 1001, f.deps());
    CHECK(!plan.ok);
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::ERROR_JSON));
    CHECK(f.userStore.authFriendApplyArgs.empty());  // 非法 action 不落库
    CHECK(f.userStore.addFriendArgs.empty());
}

TESTFW_TEST(AuthFriend_peer_not_found_returns_uid_invalid)
{
    Fixture f;
    const P1::AuthFriendPlan plan = P1::ProcessAuthFriend(f.AuthRequest(2001, 1001, 1), 1001, f.deps());
    CHECK(!plan.ok);
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK(f.userStore.authFriendApplyArgs.empty());
}

TESTFW_TEST(AuthFriend_accept_updates_apply_and_adds_friend)
{
    Fixture f;
    f.userStore.users.push_back(Fixture::MakeUser(2001, "peer"));
    f.userStore.users.push_back(Fixture::MakeUser(1001, "me"));

    const P1::AuthFriendPlan plan = P1::ProcessAuthFriend(f.AuthRequest(2001, 1001, 1), 1001, f.deps());
    CHECK(plan.ok);
    CHECK_EQ(plan.peerUid, 2001);
    CHECK_EQ(plan.selfUid, 1001);
    CHECK_EQ(plan.action, P1::FRIEND_ACTION_ACCEPT);

    // 回包带申请人资料，客户端据此更新申请列表
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(plan.response["uid"].asInt(), 2001);
    CHECK_EQ(plan.response["from_uid"].asInt(), 2001);
    CHECK_EQ(plan.response["to_uid"].asInt(), 1001);
    CHECK_EQ(plan.response["name"].asString(), std::string("peer"));
    CHECK_EQ(plan.response["action"].asInt(), P1::FRIEND_ACTION_ACCEPT);

    CHECK_EQ(f.userStore.authFriendApplyArgs.size(), 1u);
    CHECK_EQ(std::get<0>(f.userStore.authFriendApplyArgs[0]), 2001);
    CHECK_EQ(std::get<1>(f.userStore.authFriendApplyArgs[0]), 1001);
    CHECK_EQ(std::get<2>(f.userStore.authFriendApplyArgs[0]), P1::APPLY_STATUS_ACCEPTED);
    CHECK_EQ(f.userStore.addFriendArgs.size(), 1u);
    CHECK_EQ(std::get<2>(f.userStore.addFriendArgs[0]), std::string("remark"));

    // 通知申请人：同意时带处理者（本人）资料
    CHECK_EQ(plan.notify["action"].asInt(), P1::FRIEND_ACTION_ACCEPT);
    CHECK_EQ(plan.notify["from_uid"].asInt(), 2001);
    CHECK_EQ(plan.notify["to_uid"].asInt(), 1001);
    CHECK_EQ(plan.notify["name"].asString(), std::string("me"));
}

TESTFW_TEST(AuthFriend_reject_does_not_add_friend)
{
    Fixture f;
    f.userStore.users.push_back(Fixture::MakeUser(2001, "peer"));
    f.userStore.users.push_back(Fixture::MakeUser(1001, "me"));

    const P1::AuthFriendPlan plan = P1::ProcessAuthFriend(f.AuthRequest(2001, 1001, 2), 1001, f.deps());
    CHECK(plan.ok);
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(plan.response["action"].asInt(), P1::FRIEND_ACTION_REJECT);
    CHECK_EQ(f.userStore.authFriendApplyArgs.size(), 1u);
    CHECK_EQ(std::get<2>(f.userStore.authFriendApplyArgs[0]), P1::APPLY_STATUS_REJECTED);
    CHECK(f.userStore.addFriendArgs.empty());   // 拒绝不建立好友关系
    CHECK(!plan.notify.isMember("name"));       // 拒绝通知不带资料
    CHECK_EQ(plan.notify["error"].asInt(), Ec(ErrorCodes::SUCCESS));
}

TESTFW_TEST(AuthFriend_apply_update_failure_returns_uid_invalid)
{
    Fixture f;
    f.userStore.users.push_back(Fixture::MakeUser(2001, "peer"));
    f.userStore.users.push_back(Fixture::MakeUser(1001, "me"));
    f.userStore.authFriendApplyOk = false;

    const P1::AuthFriendPlan plan = P1::ProcessAuthFriend(f.AuthRequest(2001, 1001, 1), 1001, f.deps());
    CHECK(!plan.ok);
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK(f.userStore.addFriendArgs.empty());
}

TESTFW_TEST(AuthFriend_add_friend_failure_returns_uid_invalid)
{
    Fixture f;
    f.userStore.users.push_back(Fixture::MakeUser(2001, "peer"));
    f.userStore.users.push_back(Fixture::MakeUser(1001, "me"));
    f.userStore.addFriendOk = false;

    const P1::AuthFriendPlan plan = P1::ProcessAuthFriend(f.AuthRequest(2001, 1001, 1), 1001, f.deps());
    CHECK(!plan.ok);
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK_EQ(f.userStore.addFriendArgs.size(), 1u);
}

// —— 文本消息 ——
TESTFW_TEST(TextChatMsg_builds_response_and_extracts_msgs)
{
    Fixture f;
    const P1::TextChatPlan plan = P1::ProcessTextChatMsg(f.TextChatRequest(1001, 2001), 1001);
    CHECK_EQ(plan.fromUid, 1001);
    CHECK_EQ(plan.toUid, 2001);
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(plan.response["from_uid"].asInt(), 1001);
    CHECK_EQ(plan.response["to_uid"].asInt(), 2001);
    CHECK_EQ(plan.response["text_array"].size(), 1u);
    CHECK_EQ(plan.msgs.size(), 1u);
    CHECK_EQ(plan.msgs[0].first, std::string("m1"));
    CHECK_EQ(plan.msgs[0].second, std::string("hello"));
}

TESTFW_TEST(TextChatMsg_rejects_spoofed_sender)
{
    Fixture f;
    const P1::TextChatPlan plan = P1::ProcessTextChatMsg(
        f.TextChatRequest(2001, 1002), 1001);
    CHECK(plan.msgs.empty());
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
}

TESTFW_TEST(TextChatMsg_handles_missing_or_bad_text_array)
{
    Fixture f;
    Json::Value req = f.TextChatRequest(1001, 2001);
    req.removeMember("text_array");
    const P1::TextChatPlan plan = P1::ProcessTextChatMsg(req, 1001);
    CHECK(plan.msgs.empty());
    CHECK_EQ(plan.response["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(plan.response["to_uid"].asInt(), 2001);
}

// —— 跨节点通知（其他 ChatServer 经 gRPC 转发到本节点） ——
TESTFW_TEST(PeerAddFriendNotify_carries_applicant_profile)
{
    const Json::Value notify = P1::BuildPeerAddFriendNotify(1001, "bob", "hi", "icon_b", 1, "nick_b");
    CHECK_EQ(notify["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(notify["apply_uid"].asInt(), 1001);
    CHECK_EQ(notify["name"].asString(), std::string("bob"));
    CHECK_EQ(notify["desc"].asString(), std::string("hi"));
    CHECK_EQ(notify["icon"].asString(), std::string("icon_b"));
    CHECK_EQ(notify["sex"].asInt(), 1);
    CHECK_EQ(notify["nick"].asString(), std::string("nick_b"));
}

TESTFW_TEST(ResolvePeerAuthAction_treats_legacy_zero_as_accept)
{
    CHECK_EQ(P1::ResolvePeerAuthAction(0), P1::FRIEND_ACTION_ACCEPT);   // 旧消息不带 action
    CHECK_EQ(P1::ResolvePeerAuthAction(1), P1::FRIEND_ACTION_ACCEPT);
    CHECK_EQ(P1::ResolvePeerAuthAction(2), P1::FRIEND_ACTION_REJECT);
    CHECK_EQ(P1::ResolvePeerAuthAction(99), P1::FRIEND_ACTION_ACCEPT); // 未知值按同意兼容
}

TESTFW_TEST(PeerTextChatNotify_packs_messages)
{
    const std::vector<std::pair<std::string, std::string>> msgs {
        { "m1", "hello" }, { "m2", "world" }
    };
    const Json::Value notify = P1::BuildPeerTextChatNotify(1001, 2001, msgs);
    CHECK_EQ(notify["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(notify["from_uid"].asInt(), 1001);
    CHECK_EQ(notify["to_uid"].asInt(), 2001);
    CHECK_EQ(notify["text_array"].size(), 2u);
    CHECK_EQ(notify["text_array"][0]["msg_id"].asString(), std::string("m1"));
    CHECK_EQ(notify["text_array"][0]["content"].asString(), std::string("hello"));
    CHECK_EQ(notify["text_array"][1]["msg_id"].asString(), std::string("m2"));
    CHECK_EQ(notify["text_array"][1]["content"].asString(), std::string("world"));
}

TESTFW_TEST(PeerTextChatNotify_empty_list_keeps_old_shape)
{
    const Json::Value notify = P1::BuildPeerTextChatNotify(1001, 2001, {});
    CHECK_EQ(notify["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(notify["text_array"].size(), 0u);   // 与旧实现一致：无消息时为空（客户端按 size() 读）
}

// —— 用户基础资料：Redis 缓存 + MySQL 回填 ——
TESTFW_TEST(LoadBaseUserInfo_hits_redis_without_touching_mysql)
{
    Fixture f;
    Json::Value cached;
    cached["uid"] = 1001;
    cached["name"] = "cached_user";
    cached["sex"] = 2;
    cached["nick"] = "cached_nick";
    f.kvStore.data[std::string(P1::USER_INFO_PREFIX) + "1001"] = cached.toStyledString();

    std::shared_ptr<UserInfo> info;
    const bool ok = P1::LoadBaseUserInfo(std::string(P1::USER_INFO_PREFIX) + "1001", 1001, info, f.deps());
    CHECK(ok);
    CHECK_EQ(f.userStore.getUserByUidCalls, 0); // 命中缓存不查 MySQL
    CHECK_EQ(info->name, std::string("cached_user"));
    CHECK_EQ(info->sex, 2);
}

TESTFW_TEST(LoadBaseUserInfo_mysql_hit_writes_cache_without_pwd)
{
    Fixture f;
    f.userStore.users.push_back(Fixture::MakeUser(1002, "db_user"));

    std::shared_ptr<UserInfo> info;
    const std::string key = std::string(P1::USER_INFO_PREFIX) + "1002";
    CHECK(P1::LoadBaseUserInfo(key, 1002, info, f.deps()));
    CHECK_EQ(f.userStore.getUserByUidCalls, 1);
    CHECK_EQ(info->name, std::string("db_user"));

    // 写回缓存：不能带 pwd（敏感字段不落 Redis）
    const auto cached = f.kvStore.data.find(key);
    CHECK(cached != f.kvStore.data.end());
    CHECK(cached->second.find("pwd") == std::string::npos);
    CHECK(cached->second.find("db_user") != std::string::npos);
}

TESTFW_TEST(LoadBaseUserInfo_cache_miss_and_db_miss_returns_false)
{
    Fixture f;
    std::shared_ptr<UserInfo> info = std::make_shared<UserInfo>();
    CHECK(!P1::LoadBaseUserInfo(std::string(P1::USER_INFO_PREFIX) + "9999", 9999, info, f.deps()));
    CHECK_EQ(f.userStore.getUserByUidCalls, 1);
}

// —— 登录流程 ——
TESTFW_TEST(Login_token_not_found_returns_uid_invalid)
{
    Fixture f;
    const P1::LoginResult r = P1::ProcessLogin(f.LoginRequest(1001, "tok"), f.deps());
    CHECK(!r.ok);
    CHECK_EQ(r.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
    CHECK_EQ(f.userStore.getApplyListCalls, 0);
}

TESTFW_TEST(Login_empty_token_is_rejected_without_consuming)
{
    Fixture f;
    f.kvStore.data[f.TokenKey(1001)] = "";
    const P1::LoginResult r = P1::ProcessLogin(f.LoginRequest(1001, ""), f.deps());
    CHECK(!r.ok);
    CHECK_EQ(r.response["error"].asInt(), Ec(ErrorCodes::TOKEN_INVALID));
    CHECK(f.kvStore.data.find(f.TokenKey(1001)) != f.kvStore.data.end());
}

TESTFW_TEST(Login_token_mismatch_returns_token_invalid)
{
    Fixture f;
    f.kvStore.data[f.TokenKey(1001)] = "tok-right";
    const P1::LoginResult r = P1::ProcessLogin(f.LoginRequest(1001, "tok-wrong"), f.deps());
    CHECK(!r.ok);
    CHECK_EQ(r.response["error"].asInt(), Ec(ErrorCodes::TOKEN_INVALID));
    CHECK_EQ(f.userStore.getUserByUidCalls, 0); // 校验未过不应查资料
    CHECK(f.kvStore.data.find(f.TokenKey(1001)) != f.kvStore.data.end()); // 错误 token 不消费
}

TESTFW_TEST(Login_user_not_found_returns_uid_invalid)
{
    Fixture f;
    f.kvStore.data[f.TokenKey(1001)] = "tok-right";
    const P1::LoginResult r = P1::ProcessLogin(f.LoginRequest(1001, "tok-right"), f.deps());
    CHECK(!r.ok);
    CHECK_EQ(r.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
}

TESTFW_TEST(Login_success_returns_profile_and_lists)
{
    Fixture f;
    f.kvStore.data[f.TokenKey(1001)] = "tok-right";
    f.userStore.users.push_back(Fixture::MakeUser(1001, "alice"));
    f.userStore.applyList.push_back(std::make_shared<ApplyInfo>(2001, "bob", "d", "i", "n", 1, 0));
    auto friend1 = std::make_shared<UserInfo>();
    friend1->uid = 3001;
    friend1->name = "carol";
    f.userStore.friendList.push_back(friend1);

    const P1::LoginResult r = P1::ProcessLogin(f.LoginRequest(1001, "tok-right"), f.deps());
    CHECK(r.ok);
    CHECK_EQ(r.uid, 1001);
    CHECK_EQ(r.name, std::string("alice"));
    CHECK_EQ(r.response["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(r.response["uid"].asInt(), 1001);
    CHECK_EQ(r.response["name"].asString(), std::string("alice"));
    CHECK_EQ(r.response["token"].asString(), std::string("tok-right"));
    CHECK_EQ(r.response["apply_list"].size(), 1u);
    CHECK_EQ(r.response["apply_list"][0]["name"].asString(), std::string("bob"));
    CHECK_EQ(r.response["friend_list"].size(), 1u);
    CHECK_EQ(r.response["friend_list"][0]["name"].asString(), std::string("carol"));
    CHECK(f.kvStore.data.find(f.TokenKey(1001)) == f.kvStore.data.end()); // token 一次性消费
}

TESTFW_TEST(Login_token_cannot_be_replayed)
{
    Fixture f;
    f.kvStore.data[f.TokenKey(1001)] = "tok-right";
    f.userStore.users.push_back(Fixture::MakeUser(1001, "alice"));

    const P1::LoginResult first = P1::ProcessLogin(f.LoginRequest(1001, "tok-right"), f.deps());
    CHECK(first.ok);

    const P1::LoginResult second = P1::ProcessLogin(f.LoginRequest(1001, "tok-right"), f.deps());
    CHECK(!second.ok);
    CHECK_EQ(second.response["error"].asInt(), Ec(ErrorCodes::UID_INVALID));
}

TESTFW_TEST(Login_success_with_empty_lists)
{
    Fixture f;
    f.kvStore.data[f.TokenKey(1001)] = "tok-right";
    f.userStore.users.push_back(Fixture::MakeUser(1001, "alice"));

    const P1::LoginResult r = P1::ProcessLogin(f.LoginRequest(1001, "tok-right"), f.deps());
    CHECK(r.ok);
    CHECK_EQ(r.response["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK(!r.response.isMember("apply_list"));   // 无申请/好友时不产出空数组
    CHECK(!r.response.isMember("friend_list"));
}

TESTFW_TEST(Login_uses_cached_profile_when_present)
{
    Fixture f;
    f.kvStore.data[f.TokenKey(1001)] = "tok-right";
    Json::Value cached;
    cached["uid"] = 1001;
    cached["name"] = "cached_alice";
    f.kvStore.data[std::string(P1::USER_INFO_PREFIX) + "1001"] = cached.toStyledString();

    const P1::LoginResult r = P1::ProcessLogin(f.LoginRequest(1001, "tok-right"), f.deps());
    CHECK(r.ok);
    CHECK_EQ(r.response["name"].asString(), std::string("cached_alice"));
    CHECK_EQ(f.userStore.getUserByUidCalls, 0);
}

// —— 登录响应：申请 / 好友列表序列化 ——
TESTFW_TEST(AppendApplyList_serializes_fields)
{
    Json::Value dst;
    std::vector<std::shared_ptr<ApplyInfo>> list;
    list.push_back(std::make_shared<ApplyInfo>(2001, "apply_a", "d_a", "i_a", "n_a", 1, 0));
    list.push_back(std::make_shared<ApplyInfo>(2002, "apply_b", "d_b", "i_b", "n_b", 0, 1));

    P1::AppendApplyList(dst, list);

    CHECK(dst["apply_list"].isArray());
    CHECK_EQ(dst["apply_list"].size(), 2u);
    CHECK_EQ(dst["apply_list"][0]["uid"].asInt(), 2001);
    CHECK_EQ(dst["apply_list"][0]["name"].asString(), std::string("apply_a"));
    CHECK_EQ(dst["apply_list"][0]["desc"].asString(), std::string("d_a"));
    CHECK_EQ(dst["apply_list"][0]["icon"].asString(), std::string("i_a"));
    CHECK_EQ(dst["apply_list"][0]["nick"].asString(), std::string("n_a"));
    CHECK_EQ(dst["apply_list"][0]["sex"].asInt(), 1);
    CHECK_EQ(dst["apply_list"][0]["status"].asInt(), 0);
    CHECK_EQ(dst["apply_list"][1]["status"].asInt(), 1);
    // 申请列表不应携带好友列表专属字段
    CHECK(!dst["apply_list"][0].isMember("back"));
}

TESTFW_TEST(AppendFriendList_serializes_fields)
{
    Json::Value dst;
    auto f1 = std::make_shared<UserInfo>();
    f1->uid = 3001;
    f1->name = "friend_a";
    f1->nick = "n_fa";
    f1->desc = "d_fa";
    f1->icon = "i_fa";
    f1->sex = 1;
    f1->back = "back_fa";

    P1::AppendFriendList(dst, std::vector<std::shared_ptr<UserInfo>> { f1 });

    CHECK(dst["friend_list"].isArray());
    CHECK_EQ(dst["friend_list"].size(), 1u);
    CHECK_EQ(dst["friend_list"][0]["uid"].asInt(), 3001);
    CHECK_EQ(dst["friend_list"][0]["name"].asString(), std::string("friend_a"));
    CHECK_EQ(dst["friend_list"][0]["nick"].asString(), std::string("n_fa"));
    CHECK_EQ(dst["friend_list"][0]["desc"].asString(), std::string("d_fa"));
    CHECK_EQ(dst["friend_list"][0]["icon"].asString(), std::string("i_fa"));
    CHECK_EQ(dst["friend_list"][0]["sex"].asInt(), 1);
    CHECK_EQ(dst["friend_list"][0]["back"].asString(), std::string("back_fa"));
}

TESTFW_TEST(AppendLists_empty_input_keeps_dst_untouched)
{
    Json::Value dst;
    dst["error"] = 0;
    P1::AppendApplyList(dst, {});
    P1::AppendFriendList(dst, {});
    CHECK(!dst.isMember("apply_list"));  // 空列表不产出空数组字段，保持与旧逻辑一致
    CHECK(!dst.isMember("friend_list"));
    CHECK_EQ(dst["error"].asInt(), 0);
}

} // namespace
