#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <thread>

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "status_service_impl.h"
void RunServer()
{
    auto& cfg = P1::ConfigManager::GetInstance();
    static const std::string sectionName = "StatusServer";
    std::string server_address(
        cfg.GetValue<std::string>(sectionName, "Host") + ":" + cfg.GetValue<std::string>(sectionName, "Port"));
    P1::StatusServiceImpl service;

    grpc::ServerBuilder builder;
    // 监听端口和添加服务
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    // 构建并启动gRPC服务器
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        LOG_ERROR("[StatusServer] failed to listen on %s", server_address.c_str());
        return;
    }
    LOG_INFO("[StatusServer] listening on %s", server_address.c_str());

    // 创建Boost.Asio的io_context
    boost::asio::io_context io_context;
    // 创建signal_set用于捕获SIGINT
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);

    // 设置异步等待SIGINT信号
    signals.async_wait([&server](const boost::system::error_code& error, int signal_number) {
        if (!error) {
            LOG_INFO("[StatusServer] received signal, shutting down");
            server->Shutdown(); // 优雅地关闭服务器
        }
    });

    // 在单独的线程中运行io_context
    std::thread([&io_context]() { io_context.run(); }).detach();

    // 等待服务器关闭
    server->Wait();
    io_context.stop(); // 停止io_context
}

int main(int argc, char** argv)
{
    if (argc > 1) {
        P1::ConfigManager::GetInstance().Init(argv[1]);
    } else {
        P1::ConfigManager::GetInstance().Init();
    }
    try {
        RunServer();
    } catch (std::exception const& e) {
        LOG_ERROR("[StatusServer] exception:%s", e.what());
        return EXIT_FAILURE;
    }

    return 0;
}