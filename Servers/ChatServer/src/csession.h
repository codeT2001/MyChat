#ifndef _P1_SESSION_H_
#define _P1_SESSION_H_
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <mutex>
#include <queue>

#include "msg_codec.h"
#include "msg_node.h"
namespace P1 {
using namespace boost::asio;
class CSession : public std::enable_shared_from_this<CSession> {
public:
    CSession(std::shared_ptr<io_context> ioc, uint32_t id);
    ip::tcp::socket& GetSocket();
    void Start();
    void SetRemoveCallback(std::function<void(uint32_t)>&& callback);
    void InitSocketOptions();
    bool SendMessage(const char* data, size_t len, uint16_t msgId);
    bool SendMessage(const std::string& msg, uint16_t msgId);
    // 停止接收新发送任务，并在已排队消息写完后关闭连接（用于顶号踢下线）。
    void CloseAfterFlush();
    uint32_t GetSessionId() const
    {
        return sessionId_;
    }
    bool IsClosed() const
    {
        return isClose_.load();
    }
    uint32_t GetUserUid() const
    {
        return userId_.load();
    }
    bool TrySetUserUid(uint32_t uid)
    {
        uint32_t expected = 0;
        return userId_.compare_exchange_strong(expected, uid);
    }

private:
    void OnDisconnected();
    void ReadMessageHead(const boost::system::error_code& ec, std::size_t readLength);
    void ReadMessageBody(uint32_t bodySize);
    void DoWrite();
    void HandleWrite(const boost::system::error_code& ec);
    void TryCloseAfterFlush();
    void ScheduleClose();
    void Close();
    ip::tcp::socket socket_;
    std::mutex sendMtx_;
    std::queue<std::shared_ptr<MsgNode>> sendQueue_;
    std::shared_ptr<MsgNode> msgHeadNode_;
    std::shared_ptr<MsgNode> msgBodyNode_;
    std::atomic<bool> isClose_;
    std::atomic<bool> closeAfterWrite_;
    uint32_t sessionId_;
    std::atomic<uint32_t> userId_;
    std::function<void(uint32_t)> removeCallback_;
};

struct LogicNode {
public:
    LogicNode(std::shared_ptr<CSession> session, std::shared_ptr<MsgNode> msgNode)
        : session_(session), msgNode_(msgNode)
    {}
    std::shared_ptr<CSession> session_;
    std::shared_ptr<MsgNode> msgNode_;
};
} // namespace P1

#endif // _P1_SESSION_H_
