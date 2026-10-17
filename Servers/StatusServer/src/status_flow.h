#ifndef _P1_STATUS_FLOW_H_
#define _P1_STATUS_FLOW_H_

#include <cstdint>
#include <string>

#include "chat/service_interfaces.h"

namespace P1 {

// 登录 token 有效期：只需覆盖「客户端拿到 token → 完成 TCP 登录」的窗口，
// 残留 token 靠 TTL 自然过期（同时避免断开清理误删新登录 token 的竞态）
constexpr int32_t LOGIN_TOKEN_TTL_SECONDS = 600;

// 生成登录 token（UUID 字符串；线程安全，内部使用 thread_local 生成器）
std::string GenerateLoginToken();

// 写入 / 覆盖 uid 的 token（带 TTL），供 ChatServer 登录时校验
void StoreLoginToken(int32_t uid, const std::string& token, IKeyValueStore& kv);

struct StatusLoginResult {
    int32_t error = 0;
    int32_t uid = 0;
    std::string token;
};

// 校验客户端带来的 token 是否与 Redis 中的一致（纯逻辑，kv 注入便于单测）
StatusLoginResult ProcessStatusLogin(int32_t uid, const std::string& token, IKeyValueStore& kv);

} // namespace P1

#endif // _P1_STATUS_FLOW_H_
