#include "csession.h"

#include <cstring>

#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>

#include "chat/log.h"
#include "logic_system.h"
#include "msg_codec.h"
namespace P1 {
namespace {
constexpr uint32_t MAX_SENDQUEUE_SIZE = 2000;
} // namespace

CSession::CSession(std::shared_ptr<boost::asio::io_context> ioc, uint32_t id)
    : socket_(ioc->get_executor()), isClose_(false), closeAfterWrite_(false), sessionId_(id), userId_(0)
{}

void CSession::InitSocketOptions()
{
    // 禁用 Nagle 算法，降低延迟
    boost::system::error_code ec_opt;
    socket_.set_option(ip::tcp::no_delay(true), ec_opt);
    if (ec_opt) {
        LOG_ERROR("[Session] set TCP_NODELAY failed, sessionId:%u, error:%s", sessionId_, ec_opt.message().c_str());
    }

    msgHeadNode_ = std::make_shared<MsgNode>(MSG_HEAD_TOTAL_LENGTH);
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
    boost::asio::async_read(socket_, boost::asio::buffer(msgHeadNode_->GetData(), MSG_HEAD_TOTAL_LENGTH),
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

    if (ec || readLength != MSG_HEAD_TOTAL_LENGTH) {
        if (!ec) {
            LOG_ERROR("[Session] read head length mismatch, sessionId:%u, readLen:%zu", sessionId_, readLength);
        } else if (ec != boost::asio::error::operation_aborted) {
            LOG_ERROR("[Session] read head error, sessionId:%u, error:%s", sessionId_, ec.message().c_str());
        }
        Close();
        return;
    }

    msgHeadNode_->SetCurLen(MSG_HEAD_TOTAL_LENGTH);
    uint16_t msgId = 0;
    uint32_t bodySize = 0;
    DecodeMsgHead(msgHeadNode_->GetData(), msgId, bodySize);
    if (!IsValidMsgBodySize(bodySize)) {
        LOG_ERROR("[Session] invalid body size, sessionId:%u, msgId:%u, bodySize:%u", sessionId_, msgId, bodySize);
        Close();
        return;
    }

    msgBodyNode_ = std::make_shared<MsgNode>(bodySize, msgId);
    msgBodyNode_->SetCurLen(bodySize);
    ReadMessageBody(bodySize);
}

void CSession::ReadMessageBody(uint32_t bodySize)
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

bool CSession::SendMessage(const char* data, size_t len, uint16_t msgId)
{
    if (isClose_.load() || closeAfterWrite_.load()) {
        return false;
    }
    if (len > MAX_OUTBOUND_MSG_LENGTH) {
        LOG_ERROR("[Session] outbound message too large, sessionId:%u, msgId:%u, len:%zu",
            sessionId_, msgId, len);
        ScheduleClose();
        return false;
    }
    if (len > 0 && data == nullptr) {
        LOG_ERROR("[Session] outbound message has null data, sessionId:%u, msgId:%u, len:%zu",
            sessionId_, msgId, len);
        ScheduleClose();
        return false;
    }

    auto msgNode = std::make_shared<MsgNode>(
        static_cast<uint32_t>(len) + MSG_HEAD_TOTAL_LENGTH, msgId);

    EncodeMsgHead(msgNode->GetData(), msgId, static_cast<uint32_t>(len));

    if (len > 0) {
        memcpy(msgNode->GetData() + MSG_HEAD_TOTAL_LENGTH, data, len);
    }
    msgNode->SetCurLen(static_cast<uint32_t>(len) + MSG_HEAD_TOTAL_LENGTH);
    LOG_DEBUG("[Session] send, sessionId:%u, msgId:%u, len:%u", sessionId_, msgId, msgNode->GetCurLen());
    bool needStartWrite = false;
    {
        std::lock_guard<std::mutex> lock(sendMtx_);

        if (isClose_.load() || closeAfterWrite_.load()) {
            return false;
        }

        if (sendQueue_.size() >= MAX_SENDQUEUE_SIZE) {
            LOG_WARN("[Session] send queue full, message dropped, sessionId:%u, msgId:%u", sessionId_, msgId);
            return false;
        }

        sendQueue_.push(msgNode);
        needStartWrite = (sendQueue_.size() == 1);
    }

    if (needStartWrite) {
        DoWrite();
    }
    return true;
}

bool CSession::SendMessage(const std::string& msg, uint16_t msgId)
{
    return SendMessage(msg.data(), msg.length(), msgId);
}

void CSession::CloseAfterFlush()
{
    if (isClose_.load()) {
        return;
    }
    closeAfterWrite_.store(true);
    auto self = shared_from_this();
    boost::asio::post(socket_.get_executor(), [self]() { self->TryCloseAfterFlush(); });
}

void CSession::TryCloseAfterFlush()
{
    if (isClose_.load() || !closeAfterWrite_.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(sendMtx_);
        if (!sendQueue_.empty()) {
            return;
        }
    }
    Close();
}

void CSession::ScheduleClose()
{
    auto self = shared_from_this();
    boost::asio::post(socket_.get_executor(), [self]() { self->Close(); });
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
    bool needClose = false;
    {
        std::lock_guard<std::mutex> lock(sendMtx_);

        if (isClose_.load()) {
            return;
        }

        if (!sendQueue_.empty()) {
            sendQueue_.pop();
            needStartNext = !sendQueue_.empty();
        }
        needClose = closeAfterWrite_.load() && sendQueue_.empty();
    }

    if (needClose) {
        Close();
        return;
    }
    if (needStartNext) {
        DoWrite();
    }
}
} // namespace P1
