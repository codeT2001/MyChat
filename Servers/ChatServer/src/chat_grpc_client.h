#ifndef _P1_CHAT_GRPC_CLIENT_H_
#define _P1_CHAT_GRPC_CLIENT_H_

#include <grpcpp/grpcpp.h>
#include <unordered_map>
#include <memory>
#include "jsoncpp/json/json.h"
#include "jsoncpp/json/reader.h"
#include "jsoncpp/json/value.h"
#include "message.grpc.pb.h"
#include "chat/noncopyable.h"
#include "chat/rpc_stub_pool.h"

namespace P1 {
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

class ChatGrpcClient {
public:
    static ChatGrpcClient& GetInstance() { static ChatGrpcClient instance; return instance; }
    DISALLOW_COPY_MOVE(ChatGrpcClient);

    // bool GetBaseInfo(const std::string& baseKey, int32_t uid, std::shared_ptr<UserInfo>& userInfo);
    AddFriendRsp NotifyAddFriend(const std::string& serverName, const AddFriendReq& req);
    // ReplyFriendRsp ReplyFriend(const std::string& serverIp, const ReplyFriendReq& req);
    // SendChatMsgRsp SendChatMsg(const std::string& serverIp, const TextChatMsgReq& req);
    FriendAcceptedRsp NotifyFriendAccepted(const std::string& serverName, const FriendAcceptedReq& req);
    TextChatMsgRsp NotifyTextChatMsg(const std::string& serverName, const TextChatMsgReq& req,
        const Json::Value& rtvalue);
    // KickUserRsp KickUser(const std::string& serverIp, const KickUserReq& req);
    ~ChatGrpcClient();
private:
    ChatGrpcClient();
    std::unordered_map<std::string, std::unique_ptr<RpcStubPool<ChatService>>> pool_;
};

} // namespace P1

#endif // _P1_CHAT_GRPC_CLIENT_H_
