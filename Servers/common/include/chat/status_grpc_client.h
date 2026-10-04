#ifndef _P1_STATUS_GRPC_CLIENT_H_
#define _P1_STATUS_GRPC_CLIENT_H_

#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "chat/noncopyable.h"
#include "chat/rpc_stub_pool.h"

namespace P1 {
using message::GetChatServerRsp;
using message::LoginRsp;
using message::StatusService;
class StatusGrpcClient {
public:
    static StatusGrpcClient& GetInstance() { static StatusGrpcClient instance; return instance; }
    DISALLOW_COPY_MOVE(StatusGrpcClient);

    GetChatServerRsp GetChatServer(int32_t uid);
    LoginRsp Login(int uid, const std::string& token);
    ~StatusGrpcClient();

private:
    StatusGrpcClient();
    std::unique_ptr<RpcStubPool<StatusService>> pool_;
};

} // namespace P1

#endif // _P1_STATUS_GRPC_CLIENT_H_