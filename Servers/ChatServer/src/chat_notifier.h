#ifndef _P1_CHAT_NOTIFIER_H_
#define _P1_CHAT_NOTIFIER_H_

#include "chat_flow.h"
#include "message.grpc.pb.h"

namespace P1 {

// 跨节点通知的统一落地逻辑：查找本机会话并组装 JSON 推给客户端。
// gRPC 服务端与 Redis Stream 消费者共用，避免两条链路的字段/错误码不一致。
class ChatNotifier {
public:
    ChatNotifier();

    // 返回 false 表示目标用户不在本节点（离线/已断开），事件本身仍算处理成功。
    bool NotifyAddFriend(const message::AddFriendReq& req);
    bool NotifyFriendAccepted(const message::FriendAcceptedReq& req);
    bool NotifyTextChatMsg(const message::TextChatMsgReq& req);
    bool NotifyKickUser(const message::KickUserReq& req);

private:
    ChatDependencies deps_;
};

} // namespace P1

#endif // _P1_CHAT_NOTIFIER_H_
