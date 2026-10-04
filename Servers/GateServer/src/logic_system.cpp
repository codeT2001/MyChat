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

// 解析 POST 请求体并校验必填 string 字段。
// 失败时向 rsp 写入 ERROR_JSON 并返回 false（四个 POST 处理器共用）
bool ParseBody(const std::shared_ptr<HttpConnection> &conn, Json::Value &src, Json::Value &rsp,
               std::initializer_list<const char *> fields)
{
    const auto bodyStr = beast::buffers_to_string(conn->GetRequest().body().data());
    Json::Reader reader;
    if (!reader.parse(bodyStr, src)) {
        rsp["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return false;
    }
    for (const auto *field : fields) {
        if (!src.isMember(field) || !src[field].isString()) {
            rsp["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
            return false;
        }
    }
    return true;
}

// 校验邮箱验证码：不存在或已过期返回 false
bool CheckVerifyCode(const std::string &email, const std::string &code)
{
    auto stored = RedisManagerPool::GetInstance().Get(CODE_PREFIX + email);
    return stored.has_value() && stored == code;
}
} // namespace
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
    auto& response = conn->GetResponse();
    response.set(http::field::content_type, "application/json");
    Json::Value root;
    Json::Value src;
    Defer defer([&]() {
        beast::ostream(response.body()) << root.toStyledString();
    });

    if (!ParseBody(conn, src, root, {"email"})) {
        LOG_WARN("[GateServer] GetVerifyCode bad request, invalid json or missing email");
        return;
    }
    auto email = src["email"].asString();
    LOG_INFO("[GateServer] GetVerifyCode, email:%s", email.c_str());
    GetVerifyRsp rsp = VerifyGrpcClient::GetInstance().GetVerifyCode(email);
    root["error"] = rsp.error();
    root["email"] = src["email"];
}

void LogicSystem::HandleRegisterUser(HttpConnPtr conn)
{
    LOG_INFO("[GateServer] RegisterUser request");

    auto& response = conn->GetResponse();
    response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Value src;
    Defer defer([&]() {
        beast::ostream(response.body()) << root.toStyledString();
    });

    if (!ParseBody(conn, src, root, {"email", "verifyCode", "passwd"})) {
        LOG_WARN("[GateServer] RegisterUser failed, invalid json or missing fields");
        return;
    }

    std::string email = src["email"].asString();
    std::string verifyCode = src["verifyCode"].asString();

    if (!CheckVerifyCode(email, verifyCode)) {
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
    LOG_INFO("[GateServer] ResetPassword request");

    auto& response = conn->GetResponse();
    response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Value src;
    Defer defer([&]() {
        beast::ostream(response.body()) << root.toStyledString();
    });

    if (!ParseBody(conn, src, root, {"email", "verifyCode", "passwd"})) {
        LOG_WARN("[GateServer] ResetPassword failed, invalid json or missing fields");
        return;
    }

    std::string email = src["email"].asString();
    std::string verifyCode = src["verifyCode"].asString();

    if (!CheckVerifyCode(email, verifyCode)) {
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
    LOG_INFO("[GateServer] UserLogin request");

    auto& response = conn->GetResponse();
    response.set(http::field::content_type, "application/json");

    Json::Value root;
    Json::Value src;
    Defer defer([&]() {
        beast::ostream(response.body()) << root.toStyledString();
    });

    if (!ParseBody(conn, src, root, {"passwd", "name"})) {
        LOG_WARN("[GateServer] UserLogin failed, invalid json or missing fields");
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

    // 顶号策略：检测到已有会话路由时不拒绝登录，正常颁发 token 并分配节点，
    // 由 ChatServer 在 TCP 登录时踢掉旧会话。
    // 残留的路由键（上次会话未正常清理）也会在 ChatServer 重新写入时被覆盖，实现自愈。
    auto uidStr = std::to_string(userInfo.uid);
    auto onlineServer = RedisManagerPool::GetInstance().Get(USER_SERVER_PREFIX + uidStr);
    if (onlineServer.has_value() && !onlineServer->empty()) {
        LOG_INFO("[GateServer] UserLogin kick mode, user already online, uid:%d, old server:%s",
            userInfo.uid, onlineServer->c_str());
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
