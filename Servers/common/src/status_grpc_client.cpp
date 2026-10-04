#include "chat/status_grpc_client.h"

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"

namespace P1 {
namespace {
constexpr char DEFAULT_HOST[] = "127.0.0.1";
constexpr char DEFAULT_PORT[] = "50052";
constexpr size_t DEFAULT_POOL_SIZE = 5;
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
    pool_.reset(new RpcStubPool<StatusService>(DEFAULT_POOL_SIZE, host, port));
}
StatusGrpcClient::~StatusGrpcClient() = default;
GetChatServerRsp StatusGrpcClient::GetChatServer(int32_t uid)
{
    GetChatServerRsp reply;
    GetChatServerReq request;
    request.set_uid(uid);
    auto stub = pool_->GetStub();
    if (!stub) {
        LOG_ERROR("[StatusGrpcClient] GetChatServer failed, no available stub, uid:%d", uid);
        reply.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return reply;
    }
    ClientContext context;
    Status status = stub->GetChatServer(&context, request, &reply);
    if (!status.ok()) {
        LOG_ERROR("[StatusGrpcClient] GetChatServer rpc failed, uid:%d, error:%s",
            uid, status.error_message().c_str());
        reply.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    pool_->PutStub(std::move(stub));
    return reply;
}

LoginRsp StatusGrpcClient::Login(int uid, const std::string& token)
{
    LoginRsp reply;
    LoginReq request;
    request.set_uid(uid);
    request.set_token(token);

    auto stub = pool_->GetStub();
    if (!stub) {
        LOG_ERROR("[StatusGrpcClient] Login failed, no available stub, uid:%d", uid);
        reply.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return reply;
    }
    ClientContext context;
    Status status = stub->Login(&context, request, &reply);
    if (!status.ok()) {
        LOG_ERROR("[StatusGrpcClient] Login rpc failed, uid:%d, error:%s",
            uid, status.error_message().c_str());
        reply.set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
    }
    pool_->PutStub(std::move(stub));
    return reply;
}
} // namespace P1
