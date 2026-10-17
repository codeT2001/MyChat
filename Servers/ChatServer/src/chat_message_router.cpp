#include "chat_message_router.h"

#include <type_traits>

#include "chat/config_manager.h"
#include "chat/log.h"
#include "chat_event_bus.h"
#include "chat_grpc_client.h"
#include "rpc_dispatcher.h"

namespace P1 {
namespace {
template <typename Req>
void PostTask(const std::string& targetNode, bool useStream, ChatEventType type, const Req& req)
{
    const bool posted = RpcDispatcher::GetInstance().Post(targetNode, [targetNode, useStream, type, req]() {
        if (useStream) {
            ChatEventBus::GetInstance().Publish(targetNode, type, req);
            return;
        }
        if constexpr (std::is_same_v<Req, message::AddFriendReq>) {
            ChatGrpcClient::GetInstance().NotifyAddFriend(targetNode, req);
        } else if constexpr (std::is_same_v<Req, message::FriendAcceptedReq>) {
            ChatGrpcClient::GetInstance().NotifyFriendAccepted(targetNode, req);
        } else if constexpr (std::is_same_v<Req, message::TextChatMsgReq>) {
            ChatGrpcClient::GetInstance().NotifyTextChatMsg(targetNode, req);
        } else if constexpr (std::is_same_v<Req, message::KickUserReq>) {
            ChatGrpcClient::GetInstance().KickUser(targetNode, req);
        }
    });
    if (!posted) {
        LOG_ERROR("[ChatMessageRouter] enqueue failed, target:%s, type:%s",
            targetNode.c_str(), ChatEventTypeName(type));
    }
}
} // namespace

ChatMessageRouter& ChatMessageRouter::GetInstance()
{
    static ChatMessageRouter instance;
    return instance;
}

ChatMessageRouter::ChatMessageRouter()
{
    auto transport = ConfigManager::GetInstance().GetValue<std::string>("Cluster", "NotifyTransport", "stream");
    useStream_ = transport != "grpc";
    LOG_INFO("[ChatMessageRouter] notify transport:%s", useStream_ ? "redis_stream" : "grpc");
}

void ChatMessageRouter::NotifyAddFriend(const std::string& targetNode, const message::AddFriendReq& req)
{
    PostTask(targetNode, useStream_, ChatEventType::AddFriend, req);
}

void ChatMessageRouter::NotifyFriendAccepted(const std::string& targetNode,
    const message::FriendAcceptedReq& req)
{
    PostTask(targetNode, useStream_, ChatEventType::FriendAccepted, req);
}

void ChatMessageRouter::NotifyTextChatMsg(const std::string& targetNode,
    const message::TextChatMsgReq& req)
{
    PostTask(targetNode, useStream_, ChatEventType::TextChatMsg, req);
}

void ChatMessageRouter::KickUser(const std::string& targetNode, const message::KickUserReq& req)
{
    PostTask(targetNode, useStream_, ChatEventType::KickUser, req);
}

} // namespace P1
