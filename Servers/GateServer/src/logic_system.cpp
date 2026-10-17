#include "logic_system.h"

#include "chat/constants.h"
#include "chat/log.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
#include "chat/status_grpc_client.h"
#include "chat/verify_grpc_client.h"
#include "http_connection.h"
#include "jsoncpp/json/json.h"
namespace P1 {
namespace {

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

// —— 生产环境依赖适配：把 gRPC 原始响应转成与业务/测试共用的数据结构 ——
class VerifyCodeServiceAdapter : public IVerifyCodeService {
public:
    VerifyCodeResult GetVerifyCode(const std::string &email) override
    {
        GetVerifyRsp rsp = VerifyGrpcClient::GetInstance().GetVerifyCode(email);
        return VerifyCodeResult { rsp.error() };
    }
};

class ChatServerRouterAdapter : public IChatServerRouter {
public:
    ChatServerAssignment GetChatServer(int32_t uid) override
    {
        GetChatServerRsp rsp = StatusGrpcClient::GetInstance().GetChatServer(uid);
        return ChatServerAssignment { rsp.error(), rsp.host(), rsp.port(), rsp.token() };
    }
};

IVerifyCodeService &DefaultVerifyService()
{
    static VerifyCodeServiceAdapter adapter;
    return adapter;
}

IChatServerRouter &DefaultChatRouter()
{
    static ChatServerRouterAdapter adapter;
    return adapter;
}
} // namespace

LogicSystem::LogicSystem()
{
    // 绑定生产实现：MysqlManager / RedisManagerPool 直接实现对应接口，
    // 两个 gRPC 客户端经适配器转换。
    deps_.user_store = &MysqlManager::GetInstance();
    deps_.kv_store = &RedisManagerPool::GetInstance();
    deps_.verify_code = &DefaultVerifyService();
    deps_.chat_router = &DefaultChatRouter();

    RegisterGet("/get_test", [this](HttpConnPtr conn) { HandleGetTest(conn); });
    RegisterPost("/getVerifyCode", [this](HttpConnPtr conn) { HandleGetVerifyCode(conn); });
    RegisterPost("/registerUser", [this](HttpConnPtr conn) { HandleRegisterUser(conn); });
    RegisterPost("/resetPassword", [this](HttpConnPtr conn) { HandleResetPassword(conn); });
    RegisterPost("/userLogin", [this](HttpConnPtr conn) { HandleUserLogin(conn); });
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
    ctx.Response() = ProcessGetVerifyCode(ctx.Request(), deps_);
}

void LogicSystem::HandleRegisterUser(HttpConnPtr conn)
{
    LOG_INFO("[GateServer] RegisterUser request");

    HttpJsonContext ctx(conn, {"email", "verifyCode", "passwd"});
    if (!ctx.Valid()) {
        LOG_WARN("[GateServer] RegisterUser failed, invalid json or missing fields");
        return;
    }
    ctx.Response() = ProcessRegisterUser(ctx.Request(), deps_);
}

void LogicSystem::HandleResetPassword(HttpConnPtr conn)
{
    LOG_INFO("[GateServer] ResetPassword request");

    HttpJsonContext ctx(conn, {"email", "verifyCode", "passwd"});
    if (!ctx.Valid()) {
        LOG_WARN("[GateServer] ResetPassword failed, invalid json or missing fields");
        return;
    }
    ctx.Response() = ProcessResetPassword(ctx.Request(), deps_);
}

void LogicSystem::HandleUserLogin(HttpConnPtr conn)
{
    LOG_INFO("[GateServer] UserLogin request");

    HttpJsonContext ctx(conn, {"passwd", "name"});
    if (!ctx.Valid()) {
        LOG_WARN("[GateServer] UserLogin failed, invalid json or missing fields");
        return;
    }
    ctx.Response() = ProcessUserLogin(ctx.Request(), deps_);
}
} // namespace P1
