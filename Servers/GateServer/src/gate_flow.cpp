#include "gate_flow.h"

#include <cstdlib>
#include <random>

#include "chat/constants.h"
#include "chat/log.h"

namespace P1 {
namespace {

// 与 VerifyServer/constants.js 的 code_prefix 保持一致
constexpr char CODE_PREFIX[] = "code_";

// 校验邮箱验证码：不存在或已过期返回 false
bool CheckVerifyCode(IKeyValueStore* kv, const std::string& email, const std::string& code)
{
    if (kv == nullptr) {
        return false;
    }
    auto stored = kv->Get(CODE_PREFIX + email);
    return stored.has_value() && stored == code;
}

// 注册时随机分配头像：icon 存 "head_N"，客户端解析到本地资源 :/images/head_N.jpg
std::string RandomIcon()
{
    static std::mt19937 rng(std::random_device{}());
    return "head_" + std::to_string(std::uniform_int_distribution<int>(1, AVATAR_COUNT)(rng));
}

} // namespace

Json::Value ProcessGetVerifyCode(const Json::Value& req, GateDependencies deps)
{
    Json::Value root;
    if (deps.verify_code == nullptr) {
        root["error"] = static_cast<int32_t>(ErrorCodes::RPC_FAILED);
        return root;
    }
    const std::string email = req["email"].asString();
    LOG_INFO("[GateServer] GetVerifyCode, email:%s", email.c_str());
    const VerifyCodeResult rsp = deps.verify_code->GetVerifyCode(email);
    root["error"] = rsp.error;
    root["email"] = req["email"];
    return root;
}

Json::Value ProcessRegisterUser(const Json::Value& req, GateDependencies deps)
{
    Json::Value root;
    const std::string email = req["email"].asString();
    const std::string verifyCode = req["verifyCode"].asString();

    if (!CheckVerifyCode(deps.kv_store, email, verifyCode)) {
        LOG_WARN("[GateServer] RegisterUser failed, verify code not found or expired, email:%s", email.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED);
        return root;
    }

    const std::string name = req["name"].asString();
    const std::string pwd = req["passwd"].asString();
    const std::string icon = RandomIcon();
    const int uid = (deps.user_store == nullptr) ? -1 : deps.user_store->RegisterUser(name, email, pwd, icon);
    if (uid == 0 || uid == -1) {
        LOG_WARN("[GateServer] RegisterUser failed, user or email already exist, name:%s", name.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::USER_EXIST);
        return root;
    }

    LOG_INFO("[GateServer] RegisterUser success, uid:%d, email:%s", uid, email.c_str());
    root["uid"] = uid;
    root["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    root["email"] = email;
    root["icon"] = icon;
    return root;
}

Json::Value ProcessResetPassword(const Json::Value& req, GateDependencies deps)
{
    Json::Value root;
    const std::string email = req["email"].asString();
    const std::string verifyCode = req["verifyCode"].asString();

    if (!CheckVerifyCode(deps.kv_store, email, verifyCode)) {
        LOG_WARN("[GateServer] ResetPassword failed, verify code not found or expired, email:%s", email.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED);
        return root;
    }

    const std::string name = req["name"].asString();
    const std::string pwd = req["passwd"].asString();
    if (deps.user_store == nullptr || !deps.user_store->CheckEmail(name, email)) {
        LOG_WARN("[GateServer] ResetPassword failed, email not match, name:%s", name.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::EMAIL_NOT_MATCH);
        return root;
    }

    if (!deps.user_store->UpdatePassword(email, pwd)) {
        LOG_WARN("[GateServer] ResetPassword failed, update password failed, email:%s", email.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::UPDATE_PASSWORD_FAILED);
        return root;
    }

    LOG_INFO("[GateServer] ResetPassword success, email:%s", email.c_str());
    root["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    root["email"] = email;
    return root;
}

Json::Value ProcessUserLogin(const Json::Value& req, GateDependencies deps)
{
    Json::Value root;
    const std::string name = req["name"].asString();
    const std::string pwd = req["passwd"].asString();
    root["name"] = name;

    UserInfo userInfo;
    if (deps.user_store == nullptr || !deps.user_store->CheckPassword(name, pwd, userInfo)) {
        LOG_WARN("[GateServer] UserLogin failed, password not match, name:%s", name.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::PASSWORD_NOT_MATCH);
        return root;
    }
    root["uid"] = userInfo.uid;
    LOG_INFO("[GateServer] UserLogin password verified, uid:%d, name:%s", userInfo.uid, name.c_str());

    // 顶号策略：检测到已有会话路由时不拒绝登录，正常颁发 token 并分配节点，
    // 由 ChatServer 在 TCP 登录时踢掉旧会话。
    // 残留的路由键（上次会话未正常清理）也会在 ChatServer 重新写入时被覆盖，实现自愈。
    const std::string uidStr = std::to_string(userInfo.uid);
    if (deps.kv_store != nullptr) {
        auto onlineServer = deps.kv_store->Get(USER_SERVER_PREFIX + uidStr);
        if (onlineServer.has_value() && !onlineServer->empty()) {
            LOG_INFO("[GateServer] UserLogin kick mode, user already online, uid:%d, old server:%s",
                userInfo.uid, onlineServer->c_str());
        }
    }

    const ChatServerAssignment reply = (deps.chat_router == nullptr)
        ? ChatServerAssignment { static_cast<int32_t>(ErrorCodes::RPC_FAILED), "", "", "" }
        : deps.chat_router->GetChatServer(userInfo.uid);
    if (reply.error) {
        LOG_ERROR("[GateServer] UserLogin get chat server failed, uid:%d, error:%d", userInfo.uid, reply.error);
        root["error"] = static_cast<uint32_t>(ErrorCodes::RPC_FAILED);
        return root;
    }
    LOG_INFO("[GateServer] UserLogin assigned chat server, uid:%d, host:%s, port:%s",
        userInfo.uid, reply.host.c_str(), reply.port.c_str());
    root["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    root["host"] = reply.host;
    root["token"] = reply.token;
    root["port"] = std::atoi(reply.port.c_str());
    return root;
}

} // namespace P1
