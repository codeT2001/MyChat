#include <sstream>
#include <vector>
#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "chat_grpc_client.h"
#include "csession.h"
#include "user_manager.h"


namespace P1 {
using grpc::ClientContext;
using grpc::Status;
using message::ChatMsgData;
namespace {
constexpr size_t DEFAULT_POOL_SIZE = 5;
} // namespace
ChatGrpcClient::ChatGrpcClient()
{
    auto& cfg = ConfigManager::GetInstance();
    auto serverList = cfg.GetValue<std::string>("PeerServer", "servers", "");
    std::stringstream ss(serverList);
    std::string name;
    std::vector<std::string> serverListVec;
    while (std::getline(ss, name, ',')) {
        serverListVec.push_back(name);
    }
    for(auto& serverName : serverListVec) {
        if(serverName.empty()) {
            continue;
        }
        auto host = cfg.GetValue<std::string>(serverName, "Host", "");
        auto port = cfg.GetValue<std::string>(serverName, "RPCPort", "");
        if(host.empty() || port.empty()) {
            continue;
        }
        pool_[serverName] = std::make_unique<RpcStubPool<ChatService>>(DEFAULT_POOL_SIZE, host, port);
    }
}

ChatGrpcClient::~ChatGrpcClient() = default;

AddFriendRsp ChatGrpcClient::NotifyAddFriend(const std::string& serverName, const AddFriendReq& req)
{
    AddFriendRsp rsp;
    rsp.set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    rsp.set_apply_uid(req.apply_uid());
    rsp.set_to_uid(req.to_uid());

    auto iter = pool_.find(serverName);
    if (iter == pool_.end()) {
        LOG_ERROR("[ChatGrpcClient] NotifyAddFriend failed, peer server not configured:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }

    auto& pool = iter->second;
    auto stub = pool->GetStub();
    if (!stub) {
        LOG_ERROR("[ChatGrpcClient] NotifyAddFriend failed, no available stub, server:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }
    ClientContext context;
    Status status = stub->NotifyAddFriend(&context, req, &rsp);
    pool->PutStub(std::move(stub));

    if (!status.ok()) {
        LOG_ERROR("[ChatGrpcClient] NotifyAddFriend rpc failed, server:%s, error:%s",
            serverName.c_str(), status.error_message().c_str());
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

    auto iter = pool_.find(serverName);
    if (iter == pool_.end()) {
        LOG_ERROR("[ChatGrpcClient] NotifyFriendAccepted failed, peer server not configured:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }

    auto& pool = iter->second;
    auto stub = pool->GetStub();
    if (!stub) {
        LOG_ERROR("[ChatGrpcClient] NotifyFriendAccepted failed, no available stub, server:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }
    ClientContext context;
    Status status = stub->NotifyFriendAccepted(&context, req, &rsp);
    pool->PutStub(std::move(stub));

    if (!status.ok()) {
        LOG_ERROR("[ChatGrpcClient] NotifyFriendAccepted rpc failed, server:%s, error:%s",
            serverName.c_str(), status.error_message().c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return rsp;
}

TextChatMsgRsp ChatGrpcClient::NotifyTextChatMsg(const std::string& serverName, const TextChatMsgReq& req,
    const Json::Value& rtvalue)
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

    auto find_iter = pool_.find(serverName);
    if (find_iter == pool_.end()) {
        LOG_ERROR("[ChatGrpcClient] NotifyTextChatMsg failed, peer server not configured:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }

    auto& pool = find_iter->second;
    auto stub = pool->GetStub();
    if (!stub) {
        LOG_ERROR("[ChatGrpcClient] NotifyTextChatMsg failed, no available stub, server:%s", serverName.c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return rsp;
    }
    ClientContext context;
    Status status = stub->NotifyTextChatMsg(&context, req, &rsp);
    pool->PutStub(std::move(stub));
    if (!status.ok()) {
        LOG_ERROR("[ChatGrpcClient] NotifyTextChatMsg rpc failed, server:%s, error:%s",
            serverName.c_str(), status.error_message().c_str());
        rsp.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return rsp;
}
} // namespace P1
