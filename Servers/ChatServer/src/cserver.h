#ifndef _P1_CSERVER_H_
#define _P1_CSERVER_H_

// an acceptor to handle link
#include <boost/asio.hpp>
#include <memory>
#include <unordered_map>
#include <atomic>
namespace P1 {
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
class CSession;
class CServer : public std::enable_shared_from_this<CServer> {
public:
    CServer(boost::asio::io_context& io, unsigned short port);
    void StartAccept();
    std::shared_ptr<CSession> GetSession(uint32_t sessionId);

private:
    void HandleAccept(std::shared_ptr<CSession> session, const boost::system::error_code& ec);
    void RemoveSession(uint32_t sessionId);
    
    tcp::acceptor acceptor_;
    net::io_context& io_;
    std::mutex sessionMtx_;
    std::atomic<uint32_t> curId_;
    std::unordered_map<uint32_t, std::weak_ptr<CSession>> sessions_;
};
} // namespace P1

#endif // _P1_CSERVER_H_