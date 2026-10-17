#include "http_connection.h"

#include <cstdint>
#include <string>
#include <utility>

#include "chat/log.h"
#include "http_params.h"
#include "logic_system.h"
namespace P1 {

// HttpConnection
HttpConnection::HttpConnection(std::shared_ptr<boost::asio::io_context> ioc) : socket_(ioc->get_executor()) {}

void HttpConnection::Start()
{
    auto self = shared_from_this();
    http::async_read(socket_, buffer_, request_, [self](beast::error_code ec, std::size_t byteLength) {
        try {
            if (ec) {
                LOG_DEBUG("[HttpConnection] read error:%s", ec.message().c_str());
                return;
            }
            boost::ignore_unused(byteLength);
            self->HandleRequest();
            self->CheckDeadline();

        } catch (std::exception& e) {
            LOG_ERROR("[HttpConnection] handle request exception:%s", e.what());
        }
    });
}

void HttpConnection::CheckDeadline()
{
    auto self = shared_from_this();
    deadline_.async_wait([self](beast::error_code ec) {
        if (!ec) {
            self->socket_.close(ec);
        }
    });
}

void HttpConnection::WriteResponse()
{
    auto self = shared_from_this();
    response_.content_length(response_.body().size());
    http::async_write(socket_, response_, [self](beast::error_code ec, std::size_t byteLength) {
        self->socket_.shutdown(tcp::socket::shutdown_send, ec);
        self->deadline_.cancel();
    });
}

void HttpConnection::HandleRequest()
{
    LOG_DEBUG("[HttpConnection] request, method:%u, target:%s",
        static_cast<unsigned>(request_.method()), std::string(request_.target()).c_str());
    response_.version(request_.version());
    response_.keep_alive(false);
    if (request_.method() == http::verb::get) {
        PreParseGetParams();
        bool success = LogicSystem::GetInstance().HandleGet(getUrl_, shared_from_this());
        if (!success) {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "text/plain");
            beast::ostream(response_.body()) << "url not found\r\n";
        } else {
            response_.result(http::status::ok);
            response_.set(http::field::server, "GateServer");
        }
        WriteResponse();
    }

    if (request_.method() == http::verb::post) {
        bool success = LogicSystem::GetInstance().HandlePost(std::string(request_.target()), shared_from_this());
        if (!success) {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "text/plain");
            beast::ostream(response_.body()) << "url not found\r\n";
        } else {
            response_.result(http::status::ok);
            response_.set(http::field::server, "GateServer");
        }
        WriteResponse();
    }
}

void HttpConnection::PreParseGetParams()
{
    /* 提取 URI:  /get_test?key1=value1&key2=value2 —— 解析逻辑见 http_params.cpp */
    // beast 的 target() 是 beast::string_view，这里显式转成 std::string_view，
    // 保证解析逻辑（http_params）不依赖 beast
    const auto rawTarget = request_.target();
    const ParsedTarget parsed =
        ParseRequestTarget(std::string_view(rawTarget.data(), rawTarget.size()));
    getUrl_ = parsed.path;
    getParams_ = parsed.params;
}
} // namespace P1