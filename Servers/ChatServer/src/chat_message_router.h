#ifndef _P1_CHAT_MESSAGE_ROUTER_H_
#define _P1_CHAT_MESSAGE_ROUTER_H_

#include <string>

#include "chat/noncopyable.h"
#include "message.grpc.pb.h"

namespace P1 {

// 跨节点通知的统一出口：按 [Cluster] NotifyTransport 选择 Redis Stream 或直连 gRPC。
// 两种方式都投递到 RpcDispatcher，避免阻塞 LogicSystem 业务 worker。
class ChatMessageRouter {
public:
    static ChatMessageRouter& GetInstance();
    DISALLOW_COPY_MOVE(ChatMessageRouter);

    void NotifyAddFriend(const std::string& targetNode, const message::AddFriendReq& req);
    void NotifyFriendAccepted(const std::string& targetNode, const message::FriendAcceptedReq& req);
    void NotifyTextChatMsg(const std::string& targetNode, const message::TextChatMsgReq& req);
    void KickUser(const std::string& targetNode, const message::KickUserReq& req);

private:
    ChatMessageRouter();
    bool useStream_ = true;
};

} // namespace P1

#endif // _P1_CHAT_MESSAGE_ROUTER_H_
