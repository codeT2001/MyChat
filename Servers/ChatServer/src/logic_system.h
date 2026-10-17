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
#include "chat_flow.h"

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

    // 注入业务依赖（生产默认绑定真实实现；单测可替换为 fake）
    void SetDependencies(const ChatDependencies& deps) { deps_ = deps; }

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
    // 顶号：踢掉 uid 的旧会话。oldServer 为 Redis 中的旧路由节点名，
    // 与本节点相同则直接发踢下线通知，否则投递到跨节点 RPC 派发线程
    void KickOldSession(int32_t uid, const std::string& oldServer,
        const std::shared_ptr<CSession>& oldLocalSession);
    // 好友申请处理结果通知申请者：在线时按节点直发/跨节点异步 RPC。
    // notify 由 ProcessAuthFriend 组装（含同意时的处理者资料）
    void NotifyAuthResult(int32_t peer_uid, int32_t self_uid, int32_t action,
        const Json::Value& notify);
    LogicSystem();
    std::queue<std::shared_ptr<LogicNode>> msgQue_;
    std::mutex msgMtx_;
    std::condition_variable cond_;
    std::thread worker_;
    bool stop_;
    std::unordered_map<MSG_IDS, handleFunc> handleFuncs_;
    ChatDependencies deps_;
};
} // namespace P1

#endif
