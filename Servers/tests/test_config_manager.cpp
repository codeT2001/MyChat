#include "test_framework.h"

#include <fstream>
#include <string>

#include "chat/config_manager.h"

namespace {
const char* kIniPath = "/tmp/chat_config_manager_test.ini";

void WriteTempIni()
{
    std::ofstream out(kIniPath);
    out << "[Server]\n";
    out << "Host=127.0.0.1\n";
    out << "Port=9090\n";
    out << "[SelfServer]\n";
    out << "Name=chat1\n";
}
} // namespace

TESTFW_TEST(ConfigManager_ReadsValues)
{
    WriteTempIni();
    P1::ConfigManager::GetInstance().Init(kIniPath);
    CHECK_EQ(P1::ConfigManager::GetInstance().GetValue<std::string>("Server", "Host"),
        std::string("127.0.0.1"));
    CHECK_EQ(P1::ConfigManager::GetInstance().GetValue<int>("Server", "Port"), 9090);
    CHECK_EQ(P1::ConfigManager::GetInstance().GetValue<std::string>("SelfServer", "Name"),
        std::string("chat1"));
}

TESTFW_TEST(ConfigManager_FallsBackToDefault)
{
    P1::ConfigManager::GetInstance().Init(kIniPath);
    CHECK_EQ(P1::ConfigManager::GetInstance().GetValue<std::string>("NoSection", "Key", std::string("d")),
        std::string("d"));
    CHECK_EQ(P1::ConfigManager::GetInstance().GetValue<int>("Server", "Missing", 42), 42);
}
