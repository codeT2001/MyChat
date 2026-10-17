#include "test_framework.h"

#include <climits>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "server_select.h"

namespace {

using P1::ChatServer;
using P1::PickMinServer;

std::unordered_map<std::string, ChatServer> Snapshot(std::initializer_list<std::pair<const std::string, std::string>> hostPorts)
{
    std::unordered_map<std::string, ChatServer> snapshot;
    for (const auto& kv : hostPorts) {
        ChatServer server;
        server.name = kv.first;
        server.host = "127.0.0.1";
        server.port = kv.second;
        snapshot[server.name] = server;
    }
    return snapshot;
}

TESTFW_TEST(PickMinServer_empty_snapshot_returns_default)
{
    const ChatServer picked = PickMinServer({}, {});
    CHECK_EQ(picked.name, std::string(""));
    CHECK_EQ(picked.port, std::string(""));
    CHECK_EQ(picked.con_count, 0);
}

TESTFW_TEST(PickMinServer_single_node_is_picked)
{
    const auto snapshot = Snapshot({ { "server1", "50061" } });
    const ChatServer picked = PickMinServer(snapshot, { { "server1", "17" } });
    CHECK_EQ(picked.name, std::string("server1"));
    CHECK_EQ(picked.port, std::string("50061"));
    CHECK_EQ(picked.con_count, 17);
}

TESTFW_TEST(PickMinServer_chooses_least_loaded)
{
    const auto snapshot = Snapshot({ { "server1", "50061" }, { "server2", "50062" }, { "server3", "50063" } });
    const std::unordered_map<std::string, std::string> counts {
        { "server1", "30" }, { "server2", "5" }, { "server3", "42" }
    };
    const ChatServer picked = PickMinServer(snapshot, counts);
    CHECK_EQ(picked.name, std::string("server2"));
    CHECK_EQ(picked.con_count, 5);
}

TESTFW_TEST(PickMinServer_compares_counts_numerically)
{
    // "9" 应以数值 9 参与比较（而非按字符串比较，"9" > "10"）
    const auto snapshot = Snapshot({ { "server1", "50061" }, { "server2", "50062" } });
    const std::unordered_map<std::string, std::string> counts { { "server1", "9" }, { "server2", "10" } };
    CHECK_EQ(PickMinServer(snapshot, counts).name, std::string("server1"));
}

TESTFW_TEST(PickMinServer_missing_count_uses_default_load)
{
    // 某个节点没有计数时按 INT_MAX>>1 计，因此有计数的节点优先
    const auto snapshot = Snapshot({ { "server1", "50061" }, { "server2", "50062" } });
    const std::unordered_map<std::string, std::string> counts { { "server1", "99999" } };
    const ChatServer picked = PickMinServer(snapshot, counts);
    CHECK_EQ(picked.name, std::string("server1"));
    CHECK_EQ(picked.con_count, 99999);
}

TESTFW_TEST(PickMinServer_all_counts_missing_returns_first_with_default_load)
{
    const auto snapshot = Snapshot({ { "server1", "50061" } });
    const ChatServer picked = PickMinServer(snapshot, {});
    CHECK_EQ(picked.name, std::string("server1"));
    CHECK_EQ(picked.con_count, INT_MAX >> 1);
}

TESTFW_TEST(PickMinServer_ignores_extra_count_entries)
{
    // count_map 里存在名为 "name" 的键（Redis 里可能有其它字段）时不应干扰选点
    const auto snapshot = Snapshot({ { "server1", "50061" }, { "server2", "50062" } });
    const std::unordered_map<std::string, std::string> counts {
        { "server1", "8" }, { "server2", "3" }, { "name", "0" }, { "host", "0" }
    };
    CHECK_EQ(PickMinServer(snapshot, counts).name, std::string("server2"));
}

} // namespace
