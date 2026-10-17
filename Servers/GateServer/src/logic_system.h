#ifndef _P1_LOGIC_SYSTEM_H_
#define _P1_LOGIC_SYSTEM_H_

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <functional>
#include <map>
#include <memory>

#include "chat/noncopyable.h"
#include "gate_flow.h"
namespace P1 {
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class HttpConnection;

class LogicSystem {
public:
    static LogicSystem& GetInstance() { static LogicSystem instance; return instance; }
    DISALLOW_COPY_MOVE(LogicSystem);

    using HttpConnPtr = std::shared_ptr<HttpConnection>;
    using HttpHandler = std::function<void(HttpConnPtr)>;

    ~LogicSystem();
    bool HandleGet(std::string path, std::shared_ptr<HttpConnection> conn);
    bool HandlePost(std::string path, std::shared_ptr<HttpConnection> conn);
    void RegisterGet(std::string url, HttpHandler handler);
    void RegisterPost(std::string url, HttpHandler handler);

    // 注入业务依赖：生产环境在构造时绑定真实实现；单测可替换为 fake。
    void SetDependencies(const GateDependencies& deps) { deps_ = deps; }

private:
    LogicSystem();
    void HandleGetTest(HttpConnPtr conn);
    void HandleGetVerifyCode(HttpConnPtr conn);
    void HandleRegisterUser(HttpConnPtr conn);
    void HandleResetPassword(HttpConnPtr conn);
    void HandleUserLogin(HttpConnPtr conn);
    GateDependencies deps_;
    std::map<std::string, HttpHandler> postHandlers_;
    std::map<std::string, HttpHandler> getHandlers_;
};
} // namespace P1

#endif // _P1_LOGIC_SYSTEM_H_
