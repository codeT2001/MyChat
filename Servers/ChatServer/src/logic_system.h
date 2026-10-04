#ifndef _P1_LOGIC_SYSTEM_H_
#define _P1_LOGIC_SYSTEM_H_

#include <boost/asio.hpp>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <thread>
#include <unordered_map>
#include "chat/constants.h"
#include "csession.h"
#include "chat/noncopyable.h"

namespace P1 {
using tcp = boost::asio::ip::tcp;
struct UserInfo;
struct ApplyInfo;
class CServer;
using handleFunc = std::function<void(std::shared_ptr<CSession>, const uint16_t, const std::string&)>;
class LogicSystem {
public:
    static LogicSystem& GetInstance() { static LogicSystem instance; return instance; }
    DISALLOW_COPY_MOVE(LogicSystem);

    ~LogicSystem();
    void PostMsgToQue(std::shared_ptr<LogicNode> node);

private:
    void DealMessage();
    void ProcessMessage(std::shared_ptr<LogicNode> node);

    void RegisterHandleCallback();
    void HandleLogin(std::shared_ptr<CSession>, const uint16_t msgId, const std::string& msg);
    void HandleSearchUser(std::shared_ptr<CSession>, const uint16_t msgId, const std::string& msg);
    void HandleAddFriend(std::shared_ptr<CSession>, const uint16_t msgId, const std::string& msg);
    void HandleAuthFriend(std::shared_ptr<CSession>, const uint16_t msgId, const std::string& msg);
    void HandleTextChatMsg(std::shared_ptr<CSession>, const uint16_t msgId, const std::string& msg);
    bool GetBaseUserInfo(const std::string baseKey, int32_t uid, std::shared_ptr<UserInfo>& userInfo);
    LogicSystem();
    std::queue<std::shared_ptr<LogicNode>> msgQue_;
    std::mutex msgMtx_;
    std::condition_variable cond_;
    std::thread worker_;
    bool stop_;
    std::unordered_map<MSG_IDS, handleFunc> handleFuncs_;
};
} // namespace P1

#endif