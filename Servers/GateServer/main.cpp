#include "chat/config_manager.h"
#include "chat/log.h"
#include "cserver.h"

int main(int argc, char* argv[])
{
    if (argc > 1) {
        P1::ConfigManager::GetInstance().Init(argv[1]);
    } else {
        P1::ConfigManager::GetInstance().Init();
    }
    try {
        const uint16_t DEFAULT_PROT = 9090;
        const uint16_t port = P1::ConfigManager::GetInstance().GetValue<uint16_t>("GateServer", "Port", DEFAULT_PROT);
        P1::net::io_context io { 1 };
        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&io](const boost::system::error_code& ec, int32_t) {
            if (!ec) {
                LOG_INFO("[GateServer] received signal, io stopping");
                io.stop();
            }
        });
        std::make_shared<P1::CServer>(io, port)->Start();
        LOG_INFO("[GateServer] listen on port:%u", port);
        io.run();
    } catch (std::exception& e) {
        LOG_ERROR("[GateServer] exception:%s", e.what());
        return EXIT_FAILURE;
    }

    return 0;
}
