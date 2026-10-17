#include "test_framework.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "chat/constants.h"
#include "chat/service_interfaces.h"
#include "status_flow.h"

namespace {

using P1::ErrorCodes;
using P1::StatusLoginResult;

int Ec(ErrorCodes code) { return static_cast<int>(code); }

// —— fake KV：内存版 Redis，额外记录最后一次 SetEx 的 TTL ——
class FakeKvStore : public P1::IKeyValueStore {
public:
    std::unordered_map<std::string, std::string> data;
    std::chrono::seconds lastTtl { 0 };
    int setExCalls = 0;

    void Set(const std::string& key, const std::string& val) override { data[key] = val; }
    void SetEx(const std::string& key, const std::string& val, std::chrono::seconds ttl) override
    {
        ++setExCalls;
        lastTtl = ttl;
        data[key] = val;
    }
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

std::string TokenKey(int32_t uid) { return std::string(P1::USER_TOKEN_PREFIX) + std::to_string(uid); }

// —— token 生成 ——
TESTFW_TEST(GenerateLoginToken_is_uuid_shaped_and_unique)
{
    const std::string a = P1::GenerateLoginToken();
    const std::string b = P1::GenerateLoginToken();
    CHECK_EQ(a.size(), 36u);              // 8-4-4-4-12
    CHECK_EQ(a[8], '-');
    CHECK_EQ(a[13], '-');
    CHECK_EQ(a[18], '-');
    CHECK_EQ(a[23], '-');
    CHECK(a != b);
}

// —— 写 token ——
TESTFW_TEST(StoreLoginToken_writes_key_with_ttl)
{
    FakeKvStore kv;
    P1::StoreLoginToken(1001, "tok-abc", kv);
    CHECK_EQ(kv.data[TokenKey(1001)], std::string("tok-abc"));
    CHECK_EQ(kv.setExCalls, 1);
    CHECK_EQ(kv.lastTtl.count(), static_cast<int64_t>(P1::LOGIN_TOKEN_TTL_SECONDS));
}

TESTFW_TEST(StoreLoginToken_overwrites_previous_token)
{
    FakeKvStore kv;
    P1::StoreLoginToken(1001, "old", kv);
    P1::StoreLoginToken(1001, "new", kv);
    CHECK_EQ(kv.data[TokenKey(1001)], std::string("new"));
}

// —— 校验 token ——
TESTFW_TEST(StatusLogin_missing_token_returns_uid_invalid)
{
    FakeKvStore kv;
    const StatusLoginResult r = P1::ProcessStatusLogin(1001, "tok", kv);
    CHECK_EQ(r.error, Ec(ErrorCodes::UID_INVALID));
}

TESTFW_TEST(StatusLogin_token_mismatch_returns_token_invalid)
{
    FakeKvStore kv;
    P1::StoreLoginToken(1001, "tok-right", kv);
    const StatusLoginResult r = P1::ProcessStatusLogin(1001, "tok-wrong", kv);
    CHECK_EQ(r.error, Ec(ErrorCodes::TOKEN_INVALID));
    CHECK_EQ(r.uid, 1001);
}

TESTFW_TEST(StatusLogin_success_echoes_uid_and_token)
{
    FakeKvStore kv;
    P1::StoreLoginToken(1001, "tok-right", kv);
    const StatusLoginResult r = P1::ProcessStatusLogin(1001, "tok-right", kv);
    CHECK_EQ(r.error, Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(r.uid, 1001);
    CHECK_EQ(r.token, std::string("tok-right"));
    CHECK(kv.data.find(TokenKey(1001)) == kv.data.end()); // 一次性消费，禁止重放
}

TESTFW_TEST(StatusLogin_token_cannot_be_replayed)
{
    FakeKvStore kv;
    P1::StoreLoginToken(1001, "tok-right", kv);
    CHECK_EQ(P1::ProcessStatusLogin(1001, "tok-right", kv).error, Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(P1::ProcessStatusLogin(1001, "tok-right", kv).error, Ec(ErrorCodes::UID_INVALID));
}

TESTFW_TEST(StatusLogin_is_per_uid)
{
    FakeKvStore kv;
    P1::StoreLoginToken(1001, "tok-1001", kv);
    P1::StoreLoginToken(1002, "tok-1002", kv);
    CHECK_EQ(P1::ProcessStatusLogin(1001, "tok-1001", kv).error, Ec(ErrorCodes::SUCCESS));
    CHECK_EQ(P1::ProcessStatusLogin(1002, "tok-1001", kv).error, Ec(ErrorCodes::TOKEN_INVALID));
    CHECK_EQ(P1::ProcessStatusLogin(1001, "", kv).error, Ec(ErrorCodes::TOKEN_INVALID));
}

TESTFW_TEST(StatusLogin_empty_token_in_store_is_rejected)
{
    // 客户端不带 token 时不应因为 Redis 里恰好是空串而放行
    FakeKvStore kv;
    P1::StoreLoginToken(1001, "", kv);
    CHECK_EQ(P1::ProcessStatusLogin(1001, "", kv).error, Ec(ErrorCodes::TOKEN_INVALID));
    CHECK(kv.data.find(TokenKey(1001)) != kv.data.end()); // 空 token 不消费有效键
}

} // namespace
