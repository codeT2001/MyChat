#ifndef _P1_GATE_FLOW_H_
#define _P1_GATE_FLOW_H_

#include <cstdint>

#include "jsoncpp/json/json.h"

#include "chat/service_interfaces.h"

namespace P1 {

// GateServer 业务逻辑所依赖的基础设施（接口指针）。
// 生产环境在 LogicSystem 构造时绑定真实实现；单测注入 fake。
struct GateDependencies {
    IUserStore* user_store = nullptr;
    IKeyValueStore* kv_store = nullptr;
    IVerifyCodeService* verify_code = nullptr;
    IChatServerRouter* chat_router = nullptr;
};

// 四个业务处理器：只依赖入参 JSON 与上面的接口，纯逻辑、无网络/DB 细节，便于单测。
// 每个函数返回完整的响应 JSON（含 "error" 字段）。
Json::Value ProcessGetVerifyCode(const Json::Value& req, GateDependencies deps);
Json::Value ProcessRegisterUser(const Json::Value& req, GateDependencies deps);
Json::Value ProcessResetPassword(const Json::Value& req, GateDependencies deps);
Json::Value ProcessUserLogin(const Json::Value& req, GateDependencies deps);

} // namespace P1

#endif // _P1_GATE_FLOW_H_
