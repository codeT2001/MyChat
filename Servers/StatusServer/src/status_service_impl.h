#ifndef _P1_STATUS_SERVER_IMPL_H_
#define _P1_STATUS_SERVER_IMPL_H_

#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <thread>
#include "server_select.h"
namespace P1 {
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using message::GetChatServerReq;
using message::GetChatServerRsp;
using message::LoginReq;
using message::LoginRsp;
using message::StatusService;

class StatusServiceImpl final : public StatusService::Service
{
public:
	StatusServiceImpl();
	~StatusServiceImpl();
	Status GetChatServer(ServerContext* context, const GetChatServerReq* request,
		GetChatServerRsp* reply) override;
	Status Login(ServerContext* context, const LoginReq* request,
		LoginRsp* reply) override;
private:
	void StartCacheRefresher(std::chrono::seconds interval);
	void StopCacheRefresher();
    void RefreshCache();
	ChatServer GetChatServer();
	std::unordered_map<std::string, ChatServer> servers_;
	mutable std::shared_mutex rwMtx_;
	std::thread refreshThread_;
	ChatServer minServer_;
	std::atomic<bool> running_{false};
};
}

#endif