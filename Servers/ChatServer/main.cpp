#include <sw/redis++/redis++.h>

#include "chat/asio_io_context_pool.h"
#include "chat/config_manager.h"
#include "chat/log.h"
#include "cserver.h"
#include "chat/redis_manager.h"
#include "chat_service_impl.h"
#include "chat/constants.h"
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
        RedisManagerPool::GetInstance().HSet(LOGIN_COUNT, serverName, "0");

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
        LOG_INFO("[Main] ChatServer listen on port:%u, name:%s", port, serverName.c_str());
        main_io.run();
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