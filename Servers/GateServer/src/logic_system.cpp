#include "logic_system.h"

#include "chat/constants.h"
#include "chat/log.h"
#include "http_connection.h"
#include "jsoncpp/json/json.h"
#include "jsoncpp/json/reader.h"
#include "jsoncpp/json/value.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
#include "chat/status_grpc_client.h"
#include "chat/verify_grpc_client.h"
namespace P1 {
namespace {
const std::string CODE_PREFIX = "code_";
}
LogicSystem::LogicSystem()
{
    RegisterGet("/get_test", LogicSystem::HandleGetTest);
    RegisterPost("/getVerifyCode", LogicSystem::HandleGetVerifyCode);
    RegisterPost("/registerUser", LogicSystem::HandleRegisterUser);
    RegisterPost("/resetPassword", LogicSystem::HandleResetPassword);
    RegisterPost("/userLogin", LogicSystem::HandleUserLogin);
}

LogicSystem::~LogicSystem() = default;

bool LogicSystem::HandleGet(std::string path, std::shared_ptr<HttpConnection> conn)
{
    if (getHandlers_.find(path) == getHandlers_.end()) {
        return false;
    }
    getHandlers_[path](conn);
    return true;
}

void LogicSystem::RegisterGet(std::string url, HttpHandler handler)
{
    getHandlers_[url] = handler;
}

bool LogicSystem::HandlePost(std::string path, std::shared_ptr<HttpConnection> conn)
{
    if (postHandlers_.find(path) == postHandlers_.end()) {
        return false;
    }
    postHandlers_[path](conn);
    return true;
}

void LogicSystem::RegisterPost(std::string url, HttpHandler handler)
{
    postHandlers_[url] = handler;
}

void LogicSystem::HandleGetTest(HttpConnPtr conn)
{
    auto& response = conn->GetResponse();
    beast::ostream(response.body()) << "recive get_test req" << std::endl;
    size_t i = 0;
    for (const auto [k, v] : conn->QueryGetParams()) {
        ++i;
        beast::ostream(response.body()) << "param " << i << " key is " << k;
        beast::ostream(response.body()) << " param " << i << " value is " << v << std::endl;
    }
}

void LogicSystem::HandleGetVerifyCode(HttpConnPtr conn)
{
    auto& request = conn->GetRequest();
    auto bodyStr = beast::buffers_to_string(request.body().data());
    auto& response = conn->GetResponse();
    response.set(http::field::content_type, "text/json");
    Json::Value root;
    Json::Reader reader;
    Json::Value src;

    if (!reader.parse(bodyStr, src) || !src.isMember("email")) {
        LOG_WARN("[GateServer] GetVerifyCode bad request, invalid json or missing email");
        root["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
    } else {
        auto email = src["email"].asString();
        LOG_INFO("[GateServer] GetVerifyCode, email:%s", email.c_str());
        GetVerifyRsp rsp = VerifyGrpcClient::GetInstance().GetVerifyCode(email);
        root["error"] = rsp.error();
        root["email"] = src["email"];
    }
    beast::ostream(response.body()) << root.toStyledString();
    return;
}

void LogicSystem::HandleRegisterUser(HttpConnPtr conn)
{
    auto& request = conn->GetRequest();
    auto bodyStr = beast::buffers_to_string(request.body().data());
    LOG_INFO("[GateServer] RegisterUser request");

    auto& response = conn->GetResponse();
    response.set(http::field::content_type, "application/json"); // 更标准的 MIME

    Json::Value root;
    Json::Reader reader;
    Json::Value src;

    // 无论从哪个分支返回，统一写 HTTP 响应体
    Defer defer([&]() {
        beast::ostream(response.body()) << root.toStyledString();
    });

    if (!reader.parse(bodyStr, src)) {
        LOG_WARN("[GateServer] RegisterUser failed, invalid json");
        root["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return;
    }

    // 字段存在性 & 类型校验
    if (!src.isMember("email") || !src["email"].isString() || !src.isMember("verifyCode") ||
        !src["verifyCode"].isString() || !src.isMember("passwd") || !src["passwd"].isString()) {
        LOG_WARN("[GateServer] RegisterUser failed, missing or invalid fields");
        root["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return;
    }

    std::string email = src["email"].asString();
    std::string verifyCode = src["verifyCode"].asString();

    // 验证码校验
    auto storedCode = RedisManagerPool::GetInstance().Get(CODE_PREFIX + email);
    if (!storedCode.has_value() || storedCode != verifyCode) {
        LOG_WARN("[GateServer] RegisterUser failed, verify code not found or expired, email:%s", email.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED);
        return;
    }

    // 检查用户是否已存在（数据库查询）
    std::string name = src["name"].asString();
    std::string pwd = src["passwd"].asString();
    std::string icon = "icon";
    int uid = MysqlMganager::GetInstance().RegisterUser(name, email, pwd, icon);
    if (uid == 0 || uid == -1) {
        LOG_WARN("[GateServer] RegisterUser failed, user or email already exist, name:%s", name.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::USER_EXIST);
        return;
    }

    // 成功响应
    LOG_INFO("[GateServer] RegisterUser success, uid:%d, email:%s", uid, email.c_str());
    root["uid"] = uid;
    root["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    root["email"] = email;
}

void LogicSystem::HandleResetPassword(HttpConnPtr conn)
{
    auto& request = conn->GetRequest();
    auto bodyStr = beast::buffers_to_string(request.body().data());
    LOG_INFO("[GateServer] ResetPassword request");

    auto& response = conn->GetResponse();
    response.set(http::field::content_type, "application/json"); // 更标准的 MIME

    Json::Value root;
    Json::Reader reader;
    Json::Value src;

    // 无论从哪个分支返回，统一写 HTTP 响应体
    Defer defer([&]() {
        beast::ostream(response.body()) << root.toStyledString();
    });

    if (!reader.parse(bodyStr, src)) {
        LOG_WARN("[GateServer] ResetPassword failed, invalid json");
        root["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return;
    }

    // 字段存在性 & 类型校验
    if (!src.isMember("email") || !src["email"].isString() || !src.isMember("verifyCode") ||
        !src["verifyCode"].isString() || !src.isMember("passwd") || !src["passwd"].isString()) {
        LOG_WARN("[GateServer] ResetPassword failed, missing or invalid fields");
        root["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return;
    }

    std::string email = src["email"].asString();
    std::string verifyCode = src["verifyCode"].asString();

    // 验证码校验
    auto storedCode = RedisManagerPool::GetInstance().Get(CODE_PREFIX + email);
    if (!storedCode.has_value() || storedCode != verifyCode) {
        LOG_WARN("[GateServer] ResetPassword failed, verify code not found or expired, email:%s", email.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED);
        return;
    }

    std::string name = src["name"].asString();
    std::string pwd = src["passwd"].asString();
    std::string icon = "icon";
    if (!MysqlMganager::GetInstance().CheckEmail(name, email)) {
        LOG_WARN("[GateServer] ResetPassword failed, email not match, name:%s", name.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::EMAIL_NOT_MATCH);
        return;
    }

    if (!MysqlMganager::GetInstance().UpdatePassword(email, pwd)) {
        LOG_WARN("[GateServer] ResetPassword failed, update password failed, email:%s", email.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::UPDATE_PASSWORD_FAILED);
        return;
    }

    LOG_INFO("[GateServer] ResetPassword success, email:%s", email.c_str());
    root["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    root["email"] = email;
}

void LogicSystem::HandleUserLogin(HttpConnPtr conn)
{
    auto& request = conn->GetRequest();
    auto bodyStr = beast::buffers_to_string(request.body().data());
    LOG_INFO("[GateServer] UserLogin request");

    auto& response = conn->GetResponse();
    response.set(http::field::content_type, "application/json"); // 更标准的 MIME

    Json::Value root;
    Json::Reader reader;
    Json::Value src;

    // 无论从哪个分支返回，统一写 HTTP 响应体
    Defer defer([&]() {
        beast::ostream(response.body()) << root.toStyledString();
    });

    if (!reader.parse(bodyStr, src)) {
        LOG_WARN("[GateServer] UserLogin failed, invalid json");
        root["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return;
    }

    // 字段存在性 & 类型校验
    if (!src.isMember("passwd") || !src["passwd"].isString() || !src.isMember("name") ||
        !src["name"].isString()) {
        LOG_WARN("[GateServer] UserLogin failed, missing or invalid fields");
        root["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return;
    }
    std::string name = src["name"].asString();
    std::string pwd = src["passwd"].asString();
    root["name"] = name;
    UserInfo userInfo;
    if (!MysqlMganager::GetInstance().CheckPassword(name, pwd, userInfo)) {
        LOG_WARN("[GateServer] UserLogin failed, password not match, name:%s", name.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::PASSWORD_NOT_MATCH);
        return;
    }
    root["uid"] = userInfo.uid;
    LOG_INFO("[GateServer] UserLogin password verified, uid:%d, name:%s", userInfo.uid, name.c_str());

    // 防重复登录：该用户已有路由记录（说明已在线）则直接拒绝，
    // 不再颁发新 token。路由键由 ChatServer 在会话断开时清理
    auto uidStr = std::to_string(userInfo.uid);
    auto onlineServer = RedisManagerPool::GetInstance().Get(USER_SERVER_PREFIX + uidStr);
    if (onlineServer.has_value() && !onlineServer->empty()) {
        LOG_WARN("[GateServer] UserLogin rejected, user already online, uid:%d, server:%s",
            userInfo.uid, onlineServer->c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::USER_ALREADY_LOGIN);
        root["online_server"] = *onlineServer;
        return;
    }

    auto reply = StatusGrpcClient::GetInstance().GetChatServer(userInfo.uid);
    if (reply.error()) {
        LOG_ERROR("[GateServer] UserLogin get chat server failed, uid:%d, error:%d",
            userInfo.uid, reply.error());
        root["error"] = static_cast<uint32_t>(ErrorCodes::RPC_FAILED);
        return;
    }
    LOG_INFO("[GateServer] UserLogin assigned chat server, uid:%d, host:%s, port:%s",
        userInfo.uid, reply.host().c_str(), reply.port().c_str());
    root["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    root["host"] = reply.host();
    root["token"] = reply.token();
    root["port"] = atoi(reply.port().c_str());
}
} // namespace P1
