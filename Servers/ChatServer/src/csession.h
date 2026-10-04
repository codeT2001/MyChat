#ifndef _P1_SESSION_H_
#define _P1_SESSION_H_
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <mutex>
#include <queue>

#include "msg_node.h"
namespace P1 {
constexpr uint32_t MAX_MSG_LENGTH = 1024 * 2;
using namespace boost::asio;
class CSession : public std::enable_shared_from_this<CSession> {
public:
    CSession(std::shared_ptr<io_context> ioc, uint32_t id);
    ip::tcp::socket& GetSocket();
    void Start();
    void SetRemoveCallback(std::function<void(uint32_t)>&& callback);
    void InitSocketOptions();
    void SendMessage(const char* data, size_t len, uint16_t msgId);
    void SendMessage(const std::string& msg, short msgId);
    uint32_t GetSessionId() const
    {
        return sessionId_;
    }
    uint32_t GetUserUid() const
    {
        return userId_;
    }
    void SetUserUid(uint32_t uid)
    {
        userId_ = uid;
    }

private:
    void OnDisconnected();
    void ReadMessageHead(const boost::system::error_code& ec, std::size_t readLength);
    void ReadMessageBody(uint16_t bodySize);
    void DoWrite();
    void HandleWrite(const boost::system::error_code& ec);
    void Close();
    ip::tcp::socket socket_;
    std::mutex sendMtx_;
    std::queue<std::shared_ptr<MsgNode>> sendQueue_;
    std::shared_ptr<MsgNode> msgHeadNode_;
    std::shared_ptr<MsgNode> msgBodyNode_;
    std::atomic<bool> isClose_;
    uint32_t sessionId_;
    uint32_t userId_;
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