#include "status_flow.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <chrono>

#include "chat/constants.h"
#include "chat/log.h"

namespace P1 {
namespace {
// random_generator 非线程安全，按线程各持一份（gRPC 多线程调用）
thread_local boost::uuids::random_generator tokenGen;
} // namespace

std::string GenerateLoginToken()
{
    return boost::uuids::to_string(tokenGen());
}

void StoreLoginToken(int32_t uid, const std::string& token, IKeyValueStore& kv)
{
    const std::string tokenKey = std::string(USER_TOKEN_PREFIX) + std::to_string(uid);
    kv.SetEx(tokenKey, token, std::chrono::seconds(LOGIN_TOKEN_TTL_SECONDS));
}

StatusLoginResult ProcessStatusLogin(int32_t uid, const std::string& token, IKeyValueStore& kv)
{
    StatusLoginResult result;
    result.uid = uid;
    result.token = token;
    if (uid <= 0) {
        LOG_WARN("[StatusServer] Login failed, invalid uid:%d", uid);
        result.error = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return result;
    }
    // 空 token 不能因为 Redis 中恰好存了空串而通过比对。
    if (token.empty()) {
        LOG_WARN("[StatusServer] Login failed, empty token, uid:%d", uid);
        result.error = static_cast<int32_t>(ErrorCodes::TOKEN_INVALID);
        return result;
    }

    const std::string tokenKey = std::string(USER_TOKEN_PREFIX) + std::to_string(uid);
    switch (kv.CompareAndDelete(tokenKey, token)) {
    case KvCompareDeleteResult::Deleted:
        result.error = static_cast<int32_t>(ErrorCodes::SUCCESS);
        return result;
    case KvCompareDeleteResult::NotFound:
        LOG_WARN("[StatusServer] Login failed, token not found, uid:%d", uid);
        result.error = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return result;
    case KvCompareDeleteResult::Mismatch:
        LOG_WARN("[StatusServer] Login failed, token mismatch, uid:%d", uid);
        result.error = static_cast<int32_t>(ErrorCodes::TOKEN_INVALID);
        return result;
    case KvCompareDeleteResult::Error:
        LOG_ERROR("[StatusServer] Login failed, token store error, uid:%d", uid);
        result.error = static_cast<int32_t>(ErrorCodes::RPC_FAILED);
        return result;
    }
    result.error = static_cast<int32_t>(ErrorCodes::RPC_FAILED);
    return result;
}

} // namespace P1
