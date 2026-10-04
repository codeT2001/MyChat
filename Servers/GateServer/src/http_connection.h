#ifndef _P1_HTTP_CONNECTION_H_
#define _P1_HTTP_CONNECTION_H_

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <map>
#include <memory>
#include <string>
namespace P1 {
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

constexpr int32_t BUF_SIZE = 8192;

class HttpConnection : public std::enable_shared_from_this<HttpConnection> {
public:
    HttpConnection(std::shared_ptr<boost::asio::io_context> ioc);
    http::response<http::dynamic_body>& GetResponse()
    {
        return response_;
    }
    http::request<http::dynamic_body>& GetRequest()
    {
        return request_;
    }

    const std::map<std::string, std::string>& QueryGetParams()
    {
        return getParams_;
    }

    tcp::socket& GetSocket()
    {
        return socket_;
    }
    void Start();

private:
    void CheckDeadline();
    void WriteResponse();
    void HandleRequest();
    void PreParseGetParams();
    tcp::socket socket_;
    beast::flat_buffer buffer_ { BUF_SIZE };
    http::request<http::dynamic_body> request_;
    http::response<http::dynamic_body> response_;
    net::steady_timer deadline_ { socket_.get_executor(), std::chrono::seconds(60) };
    std::string getUrl_;
    std::map<std::string, std::string> getParams_;
};
} // namespace P1

#endif // _P1_HTTP_CONNECTION_H_