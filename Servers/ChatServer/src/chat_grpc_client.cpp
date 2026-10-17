#include <algorithm>
#include <chrono>

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "chat/node_registry.h"
#include "chat_grpc_client.h"

namespace P1 {
using grpc::ClientContext;
using grpc::Status;
using message::ChatMsgData;
namespace {
constexpr size_t DEFAULT_POOL_SIZE = 5;
constexpr int32_t DEFAULT_RPC_DEADLINE_MS = 500;
constexpr auto ENDPOINT_CACHE_TTL = std::chrono::seconds(5);
} // namespace
ChatGrpcClient::ChatGrpcClient()
{
    deadlineMs_ = ConfigManager::GetInstance().GetValue<int32_t>("RPC", "DeadlineMs", DEFAULT_RPC_DEADLINE_MS);
    deadlineMs_ = std::max(deadlineMs_, 1);
    LOG_INFO("[ChatGrpcClient] initialized, deadline:%dms", deadlineMs_);
}

ChatGrpcClient::~ChatGrpcClient() = default;

std::optional<ChatGrpcClient::PeerEndpoint> ChatGrpcClient::ResolveEndpoint(const std::string& serverName) const
{
    // 只从 Redis 节点注册表解析对端地址。静态 [ChatServerN] 兼容已移除，
    // 需要 gRPC 直连时使用 NotifyTransport=grpc，但仍由注册表提供地址。
    auto node = ResolveChatNode(serverName);
    if (node.has_value() && !node->rpcHost.empty() && !node->rpcPort.empty()) {
        return PeerEndpoint { node->rpcHost, node->rpcPort };
    }
    LOG_WARN("[ChatGrpcClient] peer not registered, server:%s", serverName.c_str());
    return std::nullopt;
}

std::shared_ptr<RpcStubPool<ChatService>> ChatGrpcClient::GetPool(const std::string& serverName)
{
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(poolMtx_);
        auto iter = pools_.find(serverName);
        if (iter != pools_.end() && iter->second.pool &&
            now - iter->second.resolvedAt < ENDPOINT_CACHE_TTL) {
            return iter->second.pool;
        }
    }

    auto endpoint = ResolveEndpoint(serverName);
    if (!endpoint.has_value()) {
        LOG_ERROR("[ChatGrpcClient] peer not registered or configured, server:%s", serverName.c_str());
        return nullptr;
    }

    const std::string endpointKey = endpoint->host + ":" + endpoint->port;
    std::lock_guard<std::mutex> lock(poolMtx_);
    auto iter = pools_.find(serverName);
    if (iter != pools_.end() && iter->second.pool && iter->second.endpoint == endpointKey) {
        iter->second.resolvedAt = now;
        return iter->second.pool;
    }

    // 地址变化（节点重启换端口）时重建连接池；旧的 shared_ptr 由在途调用持有，不会悬空。
    auto pool = std::make_shared<RpcStubPool<ChatService>>(DEFAULT_POOL_SIZE, endpoint->host, endpoint->port);
    pools_[serverName] = PeerPool { endpointKey, now, pool };
    LOG_INFO("[ChatGrpcClient] resolved peer, server:%s, endpoint:%s", serverName.c_str(), endpointKey.c_str());
    return pool;
}

void ChatGrpcClient::Invalidate(const std::string& serverName)
{
    std::lock_guard<std::mutex> lock(poolMtx_);
    pools_.erase(serverName);
}

void ChatGrpcClient::SetDeadline(grpc::ClientContext& context) const
{
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadlineMs_));
}

AddFriendRsp ChatGrpcClient::NotifyAddFriend(const std::string& serverName, const AddFriendReq& req)
{
    AddFriendRsp rsp;
    rsp.set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    rsp.set_apply_uid(req.apply_uid());
    rsp.set_to_uid(req.to_uid());

    auto pool = GetPool(serverName);
    if (!pool) {
        LOG_ERROR("[ChatGrpcClient] NotifyAddFriend failed, no peer pool, server:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }

    auto stub = pool->GetStub();
    if (!stub) {
        LOG_ERROR("[ChatGrpcClient] NotifyAddFriend failed, no available stub, server:%s", serverName.c_str());
        Invalidate(serverName);
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }
    ClientContext context;
    SetDeadline(context);
    Status status = stub->NotifyAddFriend(&context, req, &rsp);
    pool->PutStub(std::move(stub));

    if (!status.ok()) {
        LOG_ERROR("[ChatGrpcClient] NotifyAddFriend rpc failed, server:%s, error:%s",
            serverName.c_str(), status.error_message().c_str());
        Invalidate(serverName);
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return rsp;
}

FriendAcceptedRsp ChatGrpcClient::NotifyFriendAccepted(const std::string& serverName, const FriendAcceptedReq& req)
{
    FriendAcceptedRsp rsp;
    rsp.set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    rsp.set_from_uid(req.from_uid());
    rsp.set_to_uid(req.to_uid());

    auto pool = GetPool(serverName);
    if (!pool) {
        LOG_ERROR("[ChatGrpcClient] NotifyFriendAccepted failed, no peer pool, server:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }

    auto stub = pool->GetStub();
    if (!stub) {
        LOG_ERROR("[ChatGrpcClient] NotifyFriendAccepted failed, no available stub, server:%s", serverName.c_str());
        Invalidate(serverName);
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }
    ClientContext context;
    SetDeadline(context);
    Status status = stub->NotifyFriendAccepted(&context, req, &rsp);
    pool->PutStub(std::move(stub));

    if (!status.ok()) {
        LOG_ERROR("[ChatGrpcClient] NotifyFriendAccepted rpc failed, server:%s, error:%s",
            serverName.c_str(), status.error_message().c_str());
        Invalidate(serverName);
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return rsp;
}

TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(const std::string& serverName, const TextChatMsgReq& req)
{
    TextChatMsgRsp rsp;
    rsp.set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));

    rsp.set_from_uid(req.from_uid());
    rsp.set_to_uid(req.to_uid());
    for (const auto& text_data : req.msgs()) {
        ChatMsgData* new_msg = rsp.add_msgs();
        new_msg->set_msg_id(text_data.msg_id());
        new_msg->set_msg_content(text_data.msg_content());
    }

    auto pool = GetPool(serverName);
    if (!pool) {
        LOG_ERROR("[ChatGrpcClient] NotifyTextChatMsg failed, no peer pool, server:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }

    auto stub = pool->GetStub();
    if (!stub) {
        LOG_ERROR("[ChatGrpcClient] NotifyTextChatMsg failed, no available stub, server:%s", serverName.c_str());
        Invalidate(serverName);
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }
    ClientContext context;
    SetDeadline(context);
    Status status = stub->NotifyTextChatMsg(&context, req, &rsp);
    pool->PutStub(std::move(stub));
    if (!status.ok()) {
        LOG_ERROR("[ChatGrpcClient] NotifyTextChatMsg rpc failed, server:%s, error:%s",
            serverName.c_str(), status.error_message().c_str());
        Invalidate(serverName);
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return rsp;
}

KickUserRsp ChatGrpcClient::KickUser(const std::string& serverName, const KickUserReq& req)
{
    KickUserRsp rsp;
    rsp.set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    rsp.set_uid(req.uid());

    auto pool = GetPool(serverName);
    if (!pool) {
        LOG_ERROR("[ChatGrpcClient] KickUser failed, no peer pool, server:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }

    auto stub = pool->GetStub();
    if (!stub) {
        LOG_ERROR("[ChatGrpcClient] KickUser failed, no available stub, server:%s", serverName.c_str());
        Invalidate(serverName);
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }
    ClientContext context;
    SetDeadline(context);
    Status status = stub->NotifyKickUser(&context, req, &rsp);
    pool->PutStub(std::move(stub));

    if (!status.ok()) {
        LOG_ERROR("[ChatGrpcClient] KickUser rpc failed, server:%s, error:%s",
            serverName.c_str(), status.error_message().c_str());
        Invalidate(serverName);
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return rsp;
}
} // namespace P1
