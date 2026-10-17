#include "test_framework.h"

#include <cstdint>
#include <memory>
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
    int getUserByUidCalls = 0;

    int RegisterUser(const std::string&, const std::string&, const std::string&, const std::string&) override { return -1; }
    bool CheckEmail(const std::string&, const std::string&) override { return false; }
    bool UpdatePassword(const std::string&, const std::string&) override { return false; }
    bool CheckPassword(const std::string&, const std::string&, UserInfo&) override { return false; }

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
    void HSet(const std::string&, const std::string&, const std::string&) override {}
    std::optional<std::string> HGet(const std::string&, const std::string&) override { return std::nullopt; }
    std::unordered_map<std::string, std::string> HGetAll(const std::string&) override { return {}; }
    std::optional<std::string> HIncrBy(const std::string&, const std::string&, int64_t) override { return std::nullopt; }
    bool Del(const std::string& key) override { return data.erase(key) > 0; }
};

struct Fixture {
    FakeUserStore userStore;
    FakeKvStore kvStore;

    ChatDependencies deps() { return ChatDependencies { &userStore, &kvStore }; }

    Json::Value SearchRequest(const std::string& uid)
    {
        Json::Value req;
        req["uid"] = uid;
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
