#include "csession.h"

#include <arpa/inet.h>
#include <boost/asio/write.hpp>

#include "chat/log.h"
#include "logic_system.h"
namespace P1 {
namespace {
constexpr uint32_t HEAD_TOTAL_LENGTH = 4;
constexpr uint32_t HEAD_ID_LENGTH = 2;
constexpr uint32_t HEAD_DATA_LENGTH = 2;
constexpr uint32_t MAX_SENDQUEUE_SIZE = 2000;

inline void WriteUint16Net(void* dest, uint16_t value)
{
    uint16_t net_val = htons(value);
    memcpy(dest, &net_val, sizeof(net_val));
}

inline uint16_t ReadUint16Net(const void* src)
{
    uint16_t val = 0;
    memcpy(&val, src, sizeof(val));
    return ntohs(val);
}
} // namespace

CSession::CSession(std::shared_ptr<boost::asio::io_context> ioc, uint32_t id)
    : socket_(ioc->get_executor()), isClose_(false), sessionId_(id), userId_(0)
{}

void CSession::InitSocketOptions()
{
    // 禁用 Nagle 算法，降低延迟
    boost::system::error_code ec_opt;
    socket_.set_option(ip::tcp::no_delay(true), ec_opt);
    if (ec_opt) {
        LOG_ERROR("[Session] set TCP_NODELAY failed, sessionId:%u, error:%s", sessionId_, ec_opt.message().c_str());
    }

    msgHeadNode_ = std::make_shared<MsgNode>(HEAD_TOTAL_LENGTH);
}

ip::tcp::socket& CSession::GetSocket()
{
    return socket_;
}

void CSession::Start()
{
    if (isClose_.load()) {
        return;
    }
    msgHeadNode_->SetCurLen(0);
    boost::asio::async_read(socket_, boost::asio::buffer(msgHeadNode_->GetData(), HEAD_TOTAL_LENGTH),
        [self = shared_from_this()](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            self->ReadMessageHead(ec, bytes_transferred);
        });
}

void CSession::SetRemoveCallback(std::function<void(uint32_t)>&& callback)
{
    removeCallback_ = std::move(callback);
}
void CSession::ReadMessageHead(const boost::system::error_code& ec, std::size_t readLength)
{
    if (isClose_.load()) {
        return;
    }

    if (ec || readLength != HEAD_TOTAL_LENGTH) {
        if (!ec) {
            LOG_ERROR("[Session] read head length mismatch, sessionId:%u, readLen:%zu", sessionId_, readLength);
        } else if (ec != boost::asio::error::operation_aborted) {
            LOG_ERROR("[Session] read head error, sessionId:%u, error:%s", sessionId_, ec.message().c_str());
        }
        Close();
        return;
    }

    msgHeadNode_->SetCurLen(HEAD_TOTAL_LENGTH);
    uint16_t msgId = ReadUint16Net(msgHeadNode_->GetData());
    uint16_t bodySize = ReadUint16Net(msgHeadNode_->GetData() + HEAD_ID_LENGTH);
    if (bodySize > MAX_MSG_LENGTH) {
        LOG_ERROR("[Session] invalid body size, sessionId:%u, msgId:%u, bodySize:%u", sessionId_, msgId, bodySize);
        Close();
        return;
    }

    msgBodyNode_ = std::make_shared<MsgNode>(bodySize, msgId);
    msgBodyNode_->SetCurLen(bodySize);
    ReadMessageBody(bodySize);
}

void CSession::ReadMessageBody(uint16_t bodySize)
{
    if (isClose_.load()) {
        return;
    }

    boost::asio::async_read(socket_, boost::asio::buffer(msgBodyNode_->GetData(), bodySize),
        [self = shared_from_this(), bodySize](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (self->isClose_.load()) {
                return;
            }

            if (ec || bytes_transferred != bodySize) {
                if (!ec) {
                    LOG_ERROR("[Session] read body length mismatch, sessionId:%u", self->sessionId_);
                } else if (ec != boost::asio::error::operation_aborted) {
                    LOG_ERROR("[Session] read body error, sessionId:%u, error:%s", self->sessionId_, ec.message().c_str());
                }
                self->Close();
                return;
            }
            LOG_DEBUG("[Session] recv, sessionId:%u, msgId:%u, len:%u", self->sessionId_,
                self->msgBodyNode_->GetMsgId(), bodySize);
            LogicSystem::GetInstance().PostMsgToQue(std::make_shared<LogicNode>(self, self->msgBodyNode_));
            self->Start();
        });
}

void CSession::OnDisconnected()
{
    if (removeCallback_) {
        removeCallback_(sessionId_);
    }
}

void CSession::Close()
{
    bool expected = false;
    if (!isClose_.compare_exchange_strong(expected, true)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(sendMtx_);
        std::queue<std::shared_ptr<MsgNode>> empty;
        std::swap(sendQueue_, empty);
    }

    boost::system::error_code ec;
    socket_.shutdown(ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);

    // 可选：在此处通知上层连接已断开
    OnDisconnected();
}

void CSession::SendMessage(const char* data, size_t len, uint16_t msgId)
{
    if (isClose_.load()) {
        return;
    }

    auto msgNode = std::make_shared<MsgNode>(len + HEAD_TOTAL_LENGTH, msgId);

    WriteUint16Net(msgNode->GetData(), msgId);
    WriteUint16Net(msgNode->GetData() + HEAD_ID_LENGTH, static_cast<uint16_t>(len));

    if (len > 0 && data != nullptr) {
        memcpy(msgNode->GetData() + HEAD_TOTAL_LENGTH, data, len);
    }
    msgNode->SetCurLen(len + HEAD_TOTAL_LENGTH);
    LOG_DEBUG("[Session] send, sessionId:%u, msgId:%u, len:%u", sessionId_, msgId, msgNode->GetCurLen());
    bool needStartWrite = false;
    {
        std::lock_guard<std::mutex> lock(sendMtx_);

        if (isClose_.load()) {
            return;
        }

        if (sendQueue_.size() >= MAX_SENDQUEUE_SIZE) {
            LOG_WARN("[Session] send queue full, message dropped, sessionId:%u, msgId:%u", sessionId_, msgId);
            return;
        }

        sendQueue_.push(msgNode);
        needStartWrite = (sendQueue_.size() == 1);
    }

    if (needStartWrite) {
        DoWrite();
    }
}

void CSession::SendMessage(const std::string& msg, short msgId)
{
    SendMessage(msg.data(), msg.length(), static_cast<uint16_t>(msgId));
}

void CSession::DoWrite()
{
    std::shared_ptr<MsgNode> msgNode;
    {
        std::lock_guard<std::mutex> lock(sendMtx_);

        if (isClose_.load() || sendQueue_.empty()) {
            return;
        }
        msgNode = sendQueue_.front();
    }
    boost::asio::async_write(socket_, boost::asio::buffer(msgNode->GetData(), msgNode->GetCurLen()),
        [self = shared_from_this()](
            const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) { self->HandleWrite(ec); });
}

void CSession::HandleWrite(const boost::system::error_code& ec)
{
    if (isClose_.load()) {
        return;
    }

    if (ec) {
        if (ec != boost::asio::error::operation_aborted) {
            LOG_ERROR("[Session] write error, sessionId:%u, error:%s", sessionId_, ec.message().c_str());
        }
        Close();
        return;
    }

    bool needStartNext = false;
    {
        std::lock_guard<std::mutex> lock(sendMtx_);

        if (isClose_.load()) {
            return;
        }

        if (!sendQueue_.empty()) {
            sendQueue_.pop();
            needStartNext = !sendQueue_.empty();
        }
    }

    if (needStartNext) {
        DoWrite();
    }
}
} // namespace P1