#include "chat/verify_grpc_client.h"

#include <algorithm>
#include <chrono>

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
namespace P1 {
namespace {
constexpr char DEFAULT_HOST[] = "127.0.0.1";
constexpr char DEFAULT_PORT[] = "50051";
constexpr int32_t DEFAULT_RPC_DEADLINE_MS = 500;
constexpr int32_t DEFAULT_RPC_ACQUIRE_TIMEOUT_MS = 1000;
} // namespace
using grpc::ClientContext;
using grpc::Status;
using message::GetVerifyReq;

VerifyGrpcClient::~VerifyGrpcClient() = default;

VerifyGrpcClient::VerifyGrpcClient()
{
    auto& cfg = ConfigManager::GetInstance();
    const std::string sectionName = "VerifyServer";
    auto host = cfg.GetValue<std::string>(sectionName, "Host", DEFAULT_HOST);
    auto port = cfg.GetValue<std::string>(sectionName, "Port", DEFAULT_PORT);

    deadlineMs_ = std::max(cfg.GetValue<int32_t>("RPC", "DeadlineMs", DEFAULT_RPC_DEADLINE_MS), 1);
    RpcStubPool<VerifyService>::Options options;
    const int32_t configuredPoolSize = cfg.GetValue<int32_t>(
        "RPC", "PoolSize", static_cast<int32_t>(RpcStubPool<VerifyService>::kDefaultPoolSize));
    const int32_t configuredAcquireTimeout = cfg.GetValue<int32_t>(
        "RPC", "AcquireTimeoutMs", DEFAULT_RPC_ACQUIRE_TIMEOUT_MS);
    options.poolSize = static_cast<size_t>(std::max<int32_t>(configuredPoolSize, 1));
    options.acquireTimeout = std::chrono::milliseconds(std::max<int32_t>(configuredAcquireTimeout, 1));
    pool_.reset(new RpcStubPool<VerifyService>(options, host, port));
}

void VerifyGrpcClient::SetDeadline(grpc::ClientContext& context) const
{
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadlineMs_));
}

GetVerifyRsp VerifyGrpcClient::GetVerifyCode(const std::string& email)
{
    GetVerifyRsp response;
    GetVerifyReq request;
    request.set_email(email);

    auto lease = pool_->Acquire();
    if (!lease) {
        LOG_ERROR("[VerifyGrpcClient] GetVerifyCode failed, acquire stub timeout, email:%s", email.c_str());
        response.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return response;
    }
    ClientContext context;
    SetDeadline(context);
    Status status = lease->GetVerifyCode(&context, request, &response);
    if (!status.ok()) {
        LOG_ERROR("[VerifyGrpcClient] GetVerifyCode rpc failed, email:%s, error:%s",
            email.c_str(), status.error_message().c_str());
        response.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    return response;
}
} // namespace P1
