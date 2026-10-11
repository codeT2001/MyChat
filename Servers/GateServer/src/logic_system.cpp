#include "logic_system.h"

#include "chat/constants.h"
#include "chat/log.h"
#include "http_connection.h"
#include "jsoncpp/json/json.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
#include "chat/status_grpc_client.h"
#include "chat/verify_grpc_client.h"
#include <random>
namespace P1 {
namespace {
// 与 VerifyServer/constants.js 的 code_prefix 保持一致
constexpr char CODE_PREFIX[] = "code_";

// HTTP JSON 处理器公共骨架：设置 JSON 响应头 → 解析请求体并校验必填 string 字段，
// 作用域结束时把响应体写回。解析或校验失败时置 ERROR_JSON 并令 Valid() 为 false。
// 收敛四个 POST 处理器重复的「set content-type → Json::Value src/root → Defer 写回」样板。
class HttpJsonContext {
public:
    HttpJsonContext(const std::shared_ptr<HttpConnection> &conn,
                    std::initializer_list<const char *> fields)
        : conn_(conn)
    {
        conn_->GetResponse().set(http::field::content_type, "application/json");
        const auto bodyStr = beast::buffers_to_string(conn_->GetRequest().body().data());
        Json::Reader reader;
        if (!reader.parse(bodyStr, request_)) {
            response_["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
            return;
        }
        for (const auto *field : fields) {
            if (!request_.isMember(field) || !request_[field].isString()) {
                response_["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
                return;
            }
        }
        valid_ = true;
    }

    ~HttpJsonContext()
    {
        beast::ostream(conn_->GetResponse().body()) << response_.toStyledString();
    }

    bool Valid() const { return valid_; }
    const Json::Value &Request() const { return request_; }
    Json::Value &Response() { return response_; }

private:
    std::shared_ptr<HttpConnection> conn_;
    Json::Value request_;
    Json::Value response_;
    bool valid_ = false;
};

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
    beast::ostream(response.body()) << "receive get_test req" << std::endl;
    size_t i = 0;
    for (const auto &[k, v] : conn->QueryGetParams()) {
        ++i;
        beast::ostream(response.body()) << "param " << i << " key is " << k;
        beast::ostream(response.body()) << " param " << i << " value is " << v << std::endl;
    }
}

void LogicSystem::HandleGetVerifyCode(HttpConnPtr conn)
{
    HttpJsonContext ctx(conn, {"email"});
    if (!ctx.Valid()) {
        LOG_WARN("[GateServer] GetVerifyCode bad request, invalid json or missing email");
        return;
    }
    const Json::Value& src = ctx.Request();
    Json::Value& root = ctx.Response();
    auto email = src["email"].asString();
    LOG_INFO("[GateServer] GetVerifyCode, email:%s", email.c_str());
    GetVerifyRsp rsp = VerifyGrpcClient::GetInstance().GetVerifyCode(email);
    root["error"] = rsp.error();
    root["email"] = src["email"];
}

void LogicSystem::HandleRegisterUser(HttpConnPtr conn)
{
    LOG_INFO("[GateServer] RegisterUser request");

    HttpJsonContext ctx(conn, {"email", "verifyCode", "passwd"});
    if (!ctx.Valid()) {
        LOG_WARN("[GateServer] RegisterUser failed, invalid json or missing fields");
        return;
    }
    const Json::Value& src = ctx.Request();
    Json::Value& root = ctx.Response();

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
    // 注册时随机分配头像：icon 存 "head_N"，客户端解析到本地资源 :/images/head_N.jpg
    static std::mt19937 rng(std::random_device{}());
    std::string icon = "head_" + std::to_string(std::uniform_int_distribution<int>(1, AVATAR_COUNT)(rng));
    int uid = MysqlManager::GetInstance().RegisterUser(name, email, pwd, icon);
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
    root["icon"] = icon;
}

void LogicSystem::HandleResetPassword(HttpConnPtr conn)
{
    LOG_INFO("[GateServer] ResetPassword request");

    HttpJsonContext ctx(conn, {"email", "verifyCode", "passwd"});
    if (!ctx.Valid()) {
        LOG_WARN("[GateServer] ResetPassword failed, invalid json or missing fields");
        return;
    }
    const Json::Value& src = ctx.Request();
    Json::Value& root = ctx.Response();

    std::string email = src["email"].asString();
    std::string verifyCode = src["verifyCode"].asString();

    if (!CheckVerifyCode(email, verifyCode)) {
        LOG_WARN("[GateServer] ResetPassword failed, verify code not found or expired, email:%s", email.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED);
        return;
    }

    std::string name = src["name"].asString();
    std::string pwd = src["passwd"].asString();
    if (!MysqlManager::GetInstance().CheckEmail(name, email)) {
        LOG_WARN("[GateServer] ResetPassword failed, email not match, name:%s", name.c_str());
        root["error"] = static_cast<int32_t>(ErrorCodes::EMAIL_NOT_MATCH);
        return;
    }

    if (!MysqlManager::GetInstance().UpdatePassword(email, pwd)) {
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

    HttpJsonContext ctx(conn, {"passwd", "name"});
    if (!ctx.Valid()) {
        LOG_WARN("[GateServer] UserLogin failed, invalid json or missing fields");
        return;
    }
    const Json::Value& src = ctx.Request();
    Json::Value& root = ctx.Response();
    std::string name = src["name"].asString();
    std::string pwd = src["passwd"].asString();
    root["name"] = name;
    UserInfo userInfo;
    if (!MysqlManager::GetInstance().CheckPassword(name, pwd, userInfo)) {
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
