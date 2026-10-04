#include "chat/verify_grpc_client.h"

#include <condition_variable>
#include <mutex>
#include <queue>

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
namespace P1 {
namespace {
constexpr char DEFAULT_HOST[] = "127.0.0.1";
constexpr char DEFAULT_PORT[] = "50051";
constexpr size_t DEFAULT_POOL_SIZE = 5;
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
    pool_.reset(new RpcStubPool<VerifyService>(DEFAULT_POOL_SIZE, host, port));
}
GetVerifyRsp VerifyGrpcClient::GetVerifyCode(const std::string& email)
{
    GetVerifyRsp response;
    GetVerifyReq request;
    request.set_email(email);

    auto stub = pool_->GetStub();
    if (!stub) {
        LOG_ERROR("[VerifyGrpcClient] GetVerifyCode failed, no available stub, email:%s", email.c_str());
        response.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return response;
    }
    ClientContext context;
    Status status = stub->GetVerifyCode(&context, request, &response);
    if (!status.ok()) {
        LOG_ERROR("[VerifyGrpcClient] GetVerifyCode rpc failed, email:%s, error:%s",
            email.c_str(), status.error_message().c_str());
        response.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    pool_->PutStub(std::move(stub));
    return response;
}
} // namespace P1