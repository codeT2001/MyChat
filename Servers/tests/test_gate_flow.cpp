#include "test_framework.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "chat/constants.h"
#include "chat/service_interfaces.h"
#include "gate_flow.h"
#include "jsoncpp/json/json.h"

namespace {

using P1::ChatServerAssignment;
using P1::ErrorCodes;
using P1::GateDependencies;
using P1::UserInfo;
using P1::VerifyCodeResult;

int Ec(ErrorCodes code) { return static_cast<int>(code); }

// —— fake 依赖：内存实现，替代 MySQL / Redis / gRPC ——
struct FakeUser {
    std::string name;
    std::string email;
    std::string pwd;
    int uid = 0;
};

class FakeUserStore : public P1::IUserStore {
public:
    std::vector<FakeUser> users;
    bool updateOk = true;
    int nextUid = 1001;

    int RegisterUser(const std::string& name, const std::string& email,
        const std::string& pwd, const std::string& icon) override
    {
        (void)icon;
        for (const auto& u : users) {
            if (u.name == name || u.email == email) {
                return -1; // 已存在
            }
        }
        users.push_back(FakeUser { name, email, pwd, nextUid });
        return nextUid++;
    }

    bool CheckEmail(const std::string& name, const std::string& email) override
    {
        for (const auto& u : users) {
            if (u.name == name && u.email == email) {
                return true;
            }
        }
        return false;
    }

    bool UpdatePassword(const std::string& email, const std::string& pwd) override
    {
        if (!updateOk) {
            return false;
        }
        for (auto& u : users) {
            if (u.email == email) {
                u.pwd = pwd;
                return true;
            }
        }
        return false;
    }

    bool CheckPassword(const std::string& name, const std::string& pwd, UserInfo& out) override
    {
        for (const auto& u : users) {
            if (u.name == name && u.pwd == pwd) {
                out.uid = u.uid;
                out.name = u.name;
                out.email = u.email;
                return true;
            }
        }
        return false;
    }

    bool GetApplyList(int32_t, std::vector<std::shared_ptr<P1::ApplyInfo>>&, int, int) override { return false; }
    bool GetFriendList(int32_t, std::vector<std::shared_ptr<UserInfo>>&) override { return false; }
    bool AddFriendApply(int32_t, int32_t) override { return false; }
    bool AuthFriendApply(int32_t, int32_t, int32_t) override { return false; }
    bool AddFriend(int32_t, int32_t, const std::string&) override { return false; }

    std::shared_ptr<UserInfo> GetUserByUid(uint32_t uid) override
    {
        for (const auto& u : users) {
            if (static_cast<uint32_t>(u.uid) == uid) {
                auto info = std::make_shared<UserInfo>();
                info->uid = u.uid;
                info->name = u.name;
                info->email = u.email;
                return info;
            }
        }
        return nullptr;
    }
};

class FakeKvStore : public P1::IKeyValueStore {
public:
    std::unordered_map<std::string, std::string> data;

    void Set(const std::string& key, const std::string& val) override { data[key] = val; }
    void SetEx(const std::string& key, const std::string& val, std::chrono::seconds) override { data[key] = val; }
    std::optional<std::string> Get(const std::string& key) override
    {
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

class FakeVerify : public P1::IVerifyCodeService {
public:
    int32_t error = 0;
    VerifyCodeResult GetVerifyCode(const std::string&) override { return VerifyCodeResult { error }; }
};

class FakeRouter : public P1::IChatServerRouter {
public:
    ChatServerAssignment reply { 0, "127.0.0.1", "50061", "tok-123" };
    ChatServerAssignment GetChatServer(int32_t) override { return reply; }
};

// 组装一份 JSON 请求
Json::Value Req(std::initializer_list<std::pair<const char*, std::string>> fields)
{
    Json::Value req;
    for (const auto& kv : fields) {
        req[kv.first] = kv.second;
    }
    return req;
}

struct Fixture {
    FakeUserStore userStore;
    FakeKvStore kvStore;
    FakeVerify verify;
    FakeRouter router;
    GateDependencies deps()
    {
        return GateDependencies { &userStore, &kvStore, &verify, &router };
    }
};

} // namespace

// ---------------- GetVerifyCode ----------------
TESTFW_TEST(GetVerifyCode_EchoesEmailAndError)
{
    Fixture f;
    f.verify.error = 0;
    Json::Value rsp = ProcessGetVerifyCode(Req({ { "email", "a@b.com" } }), f.deps());
    CHECK_EQ(rsp["error"].asInt(), 0);
    CHECK_EQ(rsp["email"].asString(), std::string("a@b.com"));
}

TESTFW_TEST(GetVerifyCode_PropagatesServiceError)
{
    Fixture f;
    f.verify.error = 20001;
    Json::Value rsp = ProcessGetVerifyCode(Req({ { "email", "a@b.com" } }), f.deps());
    CHECK_EQ(rsp["error"].asInt(), 20001);
}

// ---------------- RegisterUser ----------------
TESTFW_TEST(RegisterUser_RejectsWrongVerifyCode)
{
    Fixture f;
    Json::Value rsp = ProcessRegisterUser(
        Req({ { "email", "a@b.com" }, { "verifyCode", "0000" }, { "name", "alice" }, { "passwd", "p" } }),
        f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED));
}

TESTFW_TEST(RegisterUser_SucceedsWithValidCode)
{
    Fixture f;
    f.kvStore.data["code_a@b.com"] = "1234";
    Json::Value rsp = ProcessRegisterUser(
        Req({ { "email", "a@b.com" }, { "verifyCode", "1234" }, { "name", "alice" }, { "passwd", "p" } }),
        f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK(rsp["uid"].asInt() > 0);
    CHECK_EQ(rsp["email"].asString(), std::string("a@b.com"));
    CHECK(rsp["icon"].asString().rfind("head_", 0) == 0); // 前缀为 head_
    CHECK_EQ(f.userStore.users.size(), static_cast<size_t>(1));
}

TESTFW_TEST(RegisterUser_RejectsDuplicate)
{
    Fixture f;
    f.kvStore.data["code_a@b.com"] = "1234";
    f.userStore.users.push_back(FakeUser { "alice", "a@b.com", "p", 1001 });
    Json::Value rsp = ProcessRegisterUser(
        Req({ { "email", "a@b.com" }, { "verifyCode", "1234" }, { "name", "alice" }, { "passwd", "p" } }),
        f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::USER_EXIST));
}

// ---------------- ResetPassword ----------------
TESTFW_TEST(ResetPassword_RejectsWrongVerifyCode)
{
    Fixture f;
    Json::Value rsp = ProcessResetPassword(
        Req({ { "email", "a@b.com" }, { "verifyCode", "x" }, { "name", "alice" }, { "passwd", "new" } }),
        f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED));
}

TESTFW_TEST(ResetPassword_RejectsEmailMismatch)
{
    Fixture f;
    f.kvStore.data["code_a@b.com"] = "1234";
    f.userStore.users.push_back(FakeUser { "alice", "other@b.com", "p", 1001 });
    Json::Value rsp = ProcessResetPassword(
        Req({ { "email", "a@b.com" }, { "verifyCode", "1234" }, { "name", "alice" }, { "passwd", "new" } }),
        f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::EMAIL_NOT_MATCH));
}

TESTFW_TEST(ResetPassword_ReportsUpdateFailure)
{
    Fixture f;
    f.kvStore.data["code_a@b.com"] = "1234";
    f.userStore.updateOk = false;
    f.userStore.users.push_back(FakeUser { "alice", "a@b.com", "p", 1001 });
    Json::Value rsp = ProcessResetPassword(
        Req({ { "email", "a@b.com" }, { "verifyCode", "1234" }, { "name", "alice" }, { "passwd", "new" } }),
        f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::UPDATE_PASSWORD_FAILED));
}

TESTFW_TEST(ResetPassword_Succeeds)
{
    Fixture f;
    f.kvStore.data["code_a@b.com"] = "1234";
    f.userStore.users.push_back(FakeUser { "alice", "a@b.com", "p", 1001 });
    Json::Value rsp = ProcessResetPassword(
        Req({ { "email", "a@b.com" }, { "verifyCode", "1234" }, { "name", "alice" }, { "passwd", "new" } }),
        f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(f.userStore.users[0].pwd, std::string("new"));
}

// ---------------- UserLogin ----------------
TESTFW_TEST(UserLogin_RejectsWrongPassword)
{
    Fixture f;
    f.userStore.users.push_back(FakeUser { "alice", "a@b.com", "right", 1001 });
    Json::Value rsp = ProcessUserLogin(Req({ { "name", "alice" }, { "passwd", "wrong" } }), f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::PASSWORD_NOT_MATCH));
}

TESTFW_TEST(UserLogin_SucceedsAndReturnsServer)
{
    Fixture f;
    f.userStore.users.push_back(FakeUser { "alice", "a@b.com", "right", 1001 });
    f.router.reply = ChatServerAssignment { 0, "10.0.0.5", "50062", "tok-abc" };
    Json::Value rsp = ProcessUserLogin(Req({ { "name", "alice" }, { "passwd", "right" } }), f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(rsp["uid"].asInt(), 1001);
    CHECK_EQ(rsp["host"].asString(), std::string("10.0.0.5"));
    CHECK_EQ(rsp["token"].asString(), std::string("tok-abc"));
    CHECK_EQ(rsp["port"].asInt(), 50062);
}

TESTFW_TEST(UserLogin_ReportsRouterFailure)
{
    Fixture f;
    f.userStore.users.push_back(FakeUser { "alice", "a@b.com", "right", 1001 });
    f.router.reply = ChatServerAssignment { 2, "", "", "" };
    Json::Value rsp = ProcessUserLogin(Req({ { "name", "alice" }, { "passwd", "right" } }), f.deps());
    CHECK_EQ(rsp["error"].asInt(), Ec(ErrorCodes::RPC_FAILED));
}
