#ifndef _P1_LOGIC_SYSTEM_H_
#define _P1_LOGIC_SYSTEM_H_

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <functional>
#include <map>
#include <memory>

#include "chat/noncopyable.h"
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

private:
    LogicSystem();
    static void HandleGetTest(HttpConnPtr conn);
    static void HandleGetVerifyCode(HttpConnPtr conn);
    static void HandleRegisterUser(HttpConnPtr conn);
    static void HandleResetPassword(HttpConnPtr conn);
    static void HandleUserLogin(HttpConnPtr conn);
    std::map<std::string, HttpHandler> postHandlers_;
    std::map<std::string, HttpHandler> getHandlers_;
};
} // namespace _P1_LOGIC_SYSTEM_H_

#endif