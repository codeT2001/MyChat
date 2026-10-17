#ifndef _P1_VERIFY_GRPC_CLIENT_H_
#define _P1_VERIFY_GRPC_CLIENT_H_

#include <cstdint>
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "chat/noncopyable.h"
#include "chat/rpc_stub_pool.h"

namespace P1 {
using message::GetVerifyRsp;
using message::VerifyService;
class VerifyGrpcClient {
public:
    static VerifyGrpcClient& GetInstance() { static VerifyGrpcClient instance; return instance; }
    DISALLOW_COPY_MOVE(VerifyGrpcClient);

    GetVerifyRsp GetVerifyCode(const std::string& email);
    ~VerifyGrpcClient();

private:
    VerifyGrpcClient();
    void SetDeadline(grpc::ClientContext& context) const;

    std::unique_ptr<RpcStubPool<VerifyService>> pool_;
    int32_t deadlineMs_ = 500;
};

} // namespace P1

#endif
