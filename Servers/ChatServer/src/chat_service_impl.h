#ifndef _P1_CHAT_SERVICE_IMPL_H
#define _P1_CHAT_SERVICE_IMPL_H

#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
namespace P1 {
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using message::AddFriendReq;
using message::AddFriendRsp;
using message::ReplyFriendReq;
using message::ReplyFriendRsp;
using message::FriendAcceptedReq;
using message::FriendAcceptedRsp;
using message::SendChatMsgReq;
using message::SendChatMsgRsp;
using message::TextChatMsgReq;
using message::TextChatMsgRsp;
using message::KickUserReq;
using message::KickUserRsp;
using message::ChatService;

struct UserInfo;
class ChatServiceImpl final: public ChatService::Service
{
public:
    ChatServiceImpl();
    ~ChatServiceImpl();
    Status NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* reply) override;
    Status NotifyFriendAccepted(ServerContext* context, const FriendAcceptedReq* request, FriendAcceptedRsp* reply) override;
    Status NotifyTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* reply) override;
    bool GetBaseInfo(const std::string& baseKey, int32_t uid, std::shared_ptr<UserInfo>& userInfo);
private:

};

} // namespace P1

#endif // _P1_CHAT_SERVICE_IMPL_H
