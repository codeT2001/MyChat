#include "cserver.h"

#include "chat/asio_io_context_pool.h"
#include "chat/log.h"
#include "http_connection.h"
namespace P1 {
CServer::CServer(boost::asio::io_context& io, unsigned short port)
    : io_(io), acceptor_(io, tcp::endpoint(tcp::v4(), port))
{}

void CServer::Start()
{
    auto self = shared_from_this();
    std::shared_ptr<HttpConnection> conn =
        std::make_shared<HttpConnection>(AsioIOContextPool::GetInstance().GetIOService());
    acceptor_.async_accept(conn->GetSocket(), [self, conn](beast::error_code ec) {
        try {
            // 连接成功
            if (!ec) {
                conn->Start();
            } else {
                LOG_ERROR("[CServer] accept failed, error:%s", ec.message().c_str());
            }
            // 无论是否出错都要监听其他连接
            self->Start();
        } catch (std::exception& e) {
            LOG_ERROR("[CServer] accept exception:%s", e.what());
            self->Start();
        }
    });
}

} // namespace P1
