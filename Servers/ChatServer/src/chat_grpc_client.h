#ifndef _P1_CHAT_GRPC_CLIENT_H_
#define _P1_CHAT_GRPC_CLIENT_H_

#include <chrono>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

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
    TextChatMsgRsp NotifyTextChatMsg(const std::string& serverName, const TextChatMsgReq& req);
    // 顶号：通知 oldServer 踢掉 uid 的旧会话
    KickUserRsp KickUser(const std::string& serverName, const KickUserReq& req);
    ~ChatGrpcClient();
private:
    struct PeerEndpoint {
        std::string host;
        std::string port;
    };
    struct PeerPool {
        std::string endpoint;
        std::chrono::steady_clock::time_point resolvedAt;
        std::shared_ptr<RpcStubPool<ChatService>> pool;
    };

    ChatGrpcClient();
    // 只从 Redis 节点注册表解析对端；不再读取静态 [ChatServerN] 配置。
    std::optional<PeerEndpoint> ResolveEndpoint(const std::string& serverName) const;
    std::shared_ptr<RpcStubPool<ChatService>> GetPool(const std::string& serverName);
    void Invalidate(const std::string& serverName);
    void SetDeadline(grpc::ClientContext& context) const;

    std::unordered_map<std::string, PeerPool> pools_;
    mutable std::mutex poolMtx_;
    int32_t deadlineMs_ = 500;
    RpcStubPool<ChatService>::Options poolOptions_;
};

} // namespace P1

#endif // _P1_CHAT_GRPC_CLIENT_H_
