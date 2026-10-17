#include <sw/redis++/redis++.h>

#include <chrono>
#include <memory>
#include <string>

#include "chat/asio_io_context_pool.h"
#include "chat/config_manager.h"
#include "chat/log.h"
#include "chat/node_registry.h"
#include "cserver.h"
#include "chat/redis_manager.h"
#include "chat_service_impl.h"
#include "chat/constants.h"
#include "chat_event_consumer.h"
#include "rpc_dispatcher.h"
using namespace P1;
int main(int argc, char* argv[])
{
    if (argc > 1) {
        ConfigManager::GetInstance().Init(argv[1]);
    } else {
        ConfigManager::GetInstance().Init();
    }
    try {
        const uint16_t DEFAULT_PORT = 50061;
        auto& cfg = ConfigManager::GetInstance();
        auto serverName = cfg.GetValue<std::string>("SelfServer", "Name");
        const uint16_t port = cfg.GetValue<uint16_t>("SelfServer", "Port", DEFAULT_PORT);
        const std::string bindHost = cfg.GetValue<std::string>("SelfServer", "Host");
        const std::string rpcHost = cfg.GetValue<std::string>("SelfServer", "RpcHost", bindHost);
        const std::string publicHost = cfg.GetValue<std::string>("SelfServer", "PublicHost", bindHost);
        const std::string rpcPort = cfg.GetValue<std::string>("SelfServer", "RPCPort");
        if (serverName.empty() || rpcHost.empty() || rpcPort.empty()) {
            LOG_ERROR("[Main] invalid SelfServer config, name:%s, rpcHost:%s, rpcPort:%s",
                serverName.c_str(), rpcHost.c_str(), rpcPort.c_str());
            return EXIT_FAILURE;
        }

        RedisManagerPool::GetInstance().HSet(LOGIN_COUNT, serverName, "0");

        // 注册到 Redis 节点表：其他 ChatServer 通过它动态发现 gRPC 地址，
        // StatusServer 通过它获取可下发给客户端的 TCP 地址。
        ChatNodeInfo nodeInfo;
        nodeInfo.name = serverName;
        nodeInfo.rpcHost = rpcHost;
        nodeInfo.rpcPort = rpcPort;
        nodeInfo.publicHost = publicHost;
        nodeInfo.tcpPort = std::to_string(port);
        const auto nodeTtl = std::chrono::seconds(
            cfg.GetValue<int32_t>("Cluster", "NodeTTLSeconds", DEFAULT_NODE_TTL_SECONDS));
        const auto heartbeat = std::chrono::seconds(
            cfg.GetValue<int32_t>("Cluster", "HeartbeatSeconds", DEFAULT_NODE_HEARTBEAT_SECONDS));
        std::unique_ptr<ChatNodeRegistrar> registrar;
        const std::string notifyTransport =
            cfg.GetValue<std::string>("Cluster", "NotifyTransport", "stream");

        // 定义一个grpc server
        ChatServiceImpl service;
        grpc::ServerBuilder builder;
        std::string address = cfg.GetValue<std::string>("SelfServer", "Host") + ":" + cfg.GetValue<std::string>("SelfServer", "RPCPort");
        builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);
        std::unique_ptr<grpc::Server> grpc_server(builder.BuildAndStart());
        if (!grpc_server) {
            LOG_ERROR("[Main] ChatServer grpc listen failed on %s", address.c_str());
            return EXIT_FAILURE;
        }

        std::thread grpc_server_thread([&grpc_server]() {
            grpc_server->Wait();
        });

        boost::asio::io_context main_io;
        auto& pool = AsioIOContextPool::GetInstance();
        boost::asio::signal_set signals(main_io, SIGINT, SIGTERM);
        // 信号回调里只做最轻的事：让 run() 返回。pool.Stop()/grpc Shutdown 可能阻塞，
        // 放在 run() 返回后按序执行，避免 handler 卡住导致进程无法响应 SIGTERM
        signals.async_wait([&main_io](const boost::system::error_code& ec, int32_t) {
            if (!ec) {
                LOG_INFO("[Main] received signal, shutting down");
                main_io.stop();
            }
        });
        auto cserver = std::make_shared<CServer>(main_io, port);
        cserver->StartAccept();

        // 先创建 consumer group，再注册节点：避免注册后、group 建立前的消息被跳过。
        std::unique_ptr<ChatEventConsumer> consumer;
        if (notifyTransport != "grpc") {
            consumer = std::make_unique<ChatEventConsumer>(serverName);
            consumer->Start();
        }
        // 所有依赖就绪后再对外注册，避免其他节点发现一个尚不可用的地址。
        registrar = std::make_unique<ChatNodeRegistrar>(nodeInfo, nodeTtl, heartbeat);

        LOG_INFO("[Main] ChatServer listen on port:%u, name:%s", port, serverName.c_str());
        main_io.run();
        // 先停 RPC 派发线程（等待在途调用结束），再清理 Redis / gRPC。
        RpcDispatcher::GetInstance().Stop();
        if (consumer) {
            consumer->Stop();
        }
        if (registrar) {
            registrar->Stop();
        }
        RedisManagerPool::GetInstance().HDel(LOGIN_COUNT, serverName);
        pool.Stop();
        grpc_server->Shutdown();
        grpc_server_thread.join();
    } catch (std::exception& e) {
        LOG_ERROR("[Main] ChatServer exception:%s", e.what());
        return EXIT_FAILURE;
    }

    return 0;
}
