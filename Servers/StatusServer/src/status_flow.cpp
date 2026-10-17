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

    const std::string tokenKey = std::string(USER_TOKEN_PREFIX) + std::to_string(uid);
    auto tokenValue = kv.Get(tokenKey);
    if (!tokenValue.has_value()) {
        LOG_WARN("[StatusServer] Login failed, token not found, uid:%d", uid);
        result.error = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return result;
    }
    if (*tokenValue != token) {
        LOG_WARN("[StatusServer] Login failed, token mismatch, uid:%d", uid);
        result.error = static_cast<int32_t>(ErrorCodes::TOKEN_INVALID);
        return result;
    }
    result.error = static_cast<int32_t>(ErrorCodes::SUCCESS);
    return result;
}

} // namespace P1
