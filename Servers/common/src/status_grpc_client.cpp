#include "chat/status_grpc_client.h"

#include <algorithm>
#include <chrono>

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"

namespace P1 {
namespace {
constexpr char DEFAULT_HOST[] = "127.0.0.1";
constexpr char DEFAULT_PORT[] = "50052";
constexpr int32_t DEFAULT_RPC_DEADLINE_MS = 500;
constexpr int32_t DEFAULT_RPC_ACQUIRE_TIMEOUT_MS = 1000;
} // namespace

using grpc::ClientContext;
using grpc::Status;
using message::GetChatServerReq;
using message::LoginReq;
StatusGrpcClient::StatusGrpcClient()
{
    auto& cfg = ConfigManager::GetInstance();
    const std::string sectionName = "StatusServer";
    auto host = cfg.GetValue<std::string>(sectionName, "Host", DEFAULT_HOST);
    auto port = cfg.GetValue<std::string>(sectionName, "Port", DEFAULT_PORT);

    deadlineMs_ = std::max(cfg.GetValue<int32_t>("RPC", "DeadlineMs", DEFAULT_RPC_DEADLINE_MS), 1);
    RpcStubPool<StatusService>::Options options;
    const int32_t configuredPoolSize = cfg.GetValue<int32_t>(
        "RPC", "PoolSize", static_cast<int32_t>(RpcStubPool<StatusService>::kDefaultPoolSize));
    const int32_t configuredAcquireTimeout = cfg.GetValue<int32_t>(
        "RPC", "AcquireTimeoutMs", DEFAULT_RPC_ACQUIRE_TIMEOUT_MS);
    options.poolSize = static_cast<size_t>(std::max<int32_t>(configuredPoolSize, 1));
    options.acquireTimeout = std::chrono::milliseconds(std::max<int32_t>(configuredAcquireTimeout, 1));
    pool_.reset(new RpcStubPool<StatusService>(options, host, port));
}
StatusGrpcClient::~StatusGrpcClient() = default;

void StatusGrpcClient::SetDeadline(grpc::ClientContext& context) const
{
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadlineMs_));
}

GetChatServerRsp StatusGrpcClient::GetChatServer(int32_t uid)
{
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);
    auto lease = pool_->Acquire();
    if (!lease) {
        LOG_ERROR("[StatusGrpcClient] GetChatServer failed, acquire stub timeout, uid:%d", uid);
        reply.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return reply;
    }
    ClientContext context;
    SetDeadline(context);
    Status status = lease->GetChatServer(&context, request, &reply);
    if (!status.ok()) {
        LOG_ERROR("[StatusGrpcClient] GetChatServer rpc failed, uid:%d, error:%s",
            uid, status.error_message().c_str());
        reply.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return reply;
}

LoginRsp StatusGrpcClient::Login(int uid, const std::string& token)
{
    LoginRsp reply;
    LoginReq request;
    request.set_uid(uid);
    request.set_token(token);

    auto lease = pool_->Acquire();
    if (!lease) {
        LOG_ERROR("[StatusGrpcClient] Login failed, acquire stub timeout, uid:%d", uid);
        reply.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return reply;
    }
    ClientContext context;
    SetDeadline(context);
    Status status = lease->Login(&context, request, &reply);
    if (!status.ok()) {
        LOG_ERROR("[StatusGrpcClient] Login rpc failed, uid:%d, error:%s",
            uid, status.error_message().c_str());
        reply.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return reply;
}
} // namespace P1
