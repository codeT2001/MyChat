#include "http_connection.h"

#include <cassert>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>

#include "chat/log.h"
#include "logic_system.h"
namespace P1 {
namespace { // ---------- 十六进制字符 ↔ 数值 ----------

template<bool Upper = true>
constexpr char ToHex(unsigned char x) noexcept
{
    assert(x < 16);
    constexpr char base = Upper ? 'A' : 'a';
    return static_cast<char>(x > 9 ? (x - 10 + base) : (x + '0'));
}

constexpr std::optional<unsigned char> FromHex(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned char>(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned char>(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned char>(c - 'a' + 10);
    }
    return std::nullopt;
}

// ---------- URL 编码 ----------
inline std::string UrlEncode(const std::string& src, bool upper_hex = true)
{
    std::ostringstream oss;
    oss.fill('0');
    oss << std::hex << (upper_hex ? std::uppercase : std::nouppercase);

    for (unsigned char c : src) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << static_cast<char>(c);
        } else if (c == ' ') {
            oss << '+';
        } else {
            oss << '%' << (upper_hex ? ToHex<true>(c >> 4) : static_cast<char>(ToHex<false>(c >> 4)))
                << (upper_hex ? ToHex<true>(c & 0x0F) : static_cast<char>(ToHex<false>(c & 0x0F)));
        }
    }
    return oss.str();
}

// ---------- URL 解码 ----------
inline std::string UrlDecode(const std::string& src)
{
    std::ostringstream oss;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '+') {
            oss << ' ';
        } else if (src[i] == '%') {
            if (i + 2 >= src.size()) {
                oss << '%';
                continue;
            }
            auto high = FromHex(src[++i]);
            auto low = FromHex(src[++i]);
            if (!high || !low) {
                oss << '%' << src[i - 1] << src[i];
                continue;
            }
            oss << static_cast<char>((*high << 4) + *low);
        } else {
            oss << src[i];
        }
    }
    return oss.str();
}

} // namespace

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
    /* 提取 URI:  /get_test?key1=value1&key2=value2  */
    auto uri = request_.target();

    /* 分割路径与查询串 */
    auto queryPos = uri.find('?');
    if (queryPos == std::string::npos) {
        getUrl_ = std::string(uri);
        return;
    }

    getUrl_ = std::string(uri.substr(0, queryPos));
    std::string_view querySv { uri.data() + queryPos + 1, uri.size() - queryPos - 1 };

    /* 解析 key=value 对 */
    while (!querySv.empty()) {
        auto andPos = querySv.find('&');
        auto pairSv = querySv.substr(0, andPos); // 取 "&" 前一段

        auto eqPos = pairSv.find('=');
        if (eqPos != std::string_view::npos) {
            auto key = UrlDecode(std::string { pairSv.substr(0, eqPos) });
            auto value = UrlDecode(std::string { pairSv.substr(eqPos + 1) });
            getParams_[key] = value;
        }

        if (andPos == std::string_view::npos) {
            break;
        }
        querySv.remove_prefix(andPos + 1); // 跳过 "&"
    }
}
} // namespace P1