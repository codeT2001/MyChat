#ifndef _P1_CSERVER_H_
#define _P1_CSERVER_H_

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <memory>
namespace P1 {
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class CServer : public std::enable_shared_from_this<CServer> {
public:
    CServer(boost::asio::io_context& io, unsigned short port);
    void Start();

private:
    tcp::acceptor acceptor_;
    net::io_context& io_;
};
} // namespace P1

#endif // _P1_CSERVER_H_