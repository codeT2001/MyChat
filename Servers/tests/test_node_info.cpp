#include "test_framework.h"

#include <cstdint>
#include <string>

#include "chat/node_info.h"

namespace {

using P1::ChatNodeInfo;

ChatNodeInfo SampleNode()
{
    ChatNodeInfo info;
    info.name = "ChatServer1";
    info.rpcHost = "10.0.0.11";
    info.rpcPort = "50081";
    info.publicHost = "chat.example.com";
    info.tcpPort = "50061";
    info.updatedAtMs = 1700000000123LL;
    return info;
}

} // namespace

TESTFW_TEST(ChatNodeKey_has_expected_prefix)
{
    CHECK_EQ(P1::ChatNodeKey("ChatServer1"), std::string("chat:node:ChatServer1"));
}

TESTFW_TEST(ChatNodeInfo_roundtrip_preserves_all_fields)
{
    const ChatNodeInfo input = SampleNode();
    ChatNodeInfo output;
    CHECK(P1::DecodeChatNodeInfo(P1::EncodeChatNodeInfo(input), output));
    CHECK_EQ(output.name, input.name);
    CHECK_EQ(output.rpcHost, input.rpcHost);
    CHECK_EQ(output.rpcPort, input.rpcPort);
    CHECK_EQ(output.publicHost, input.publicHost);
    CHECK_EQ(output.tcpPort, input.tcpPort);
    CHECK_EQ(output.updatedAtMs, input.updatedAtMs);
}

TESTFW_TEST(ChatNodeInfo_decode_defaults_public_host_to_rpc_host)
{
    const std::string payload =
        R"({"name":"ChatServer2","rpcHost":"127.0.0.1","rpcPort":"50082","tcpPort":"50062"})";
    ChatNodeInfo info;
    CHECK(P1::DecodeChatNodeInfo(payload, info));
    CHECK_EQ(info.publicHost, std::string("127.0.0.1"));
}

TESTFW_TEST(ChatNodeInfo_decode_rejects_missing_required_fields)
{
    ChatNodeInfo info;
    CHECK(!P1::DecodeChatNodeInfo(
        R"({"name":"ChatServer2","rpcHost":"127.0.0.1","rpcPort":"50082"})", info));
    CHECK(!P1::DecodeChatNodeInfo(
        R"({"name":"ChatServer2","rpcPort":"50082","tcpPort":"50062"})", info));
    CHECK(!P1::DecodeChatNodeInfo(
        R"({"name":"ChatServer2","rpcHost":"127.0.0.1","tcpPort":"50062"})", info));
}

TESTFW_TEST(ChatNodeInfo_decode_rejects_invalid_json)
{
    ChatNodeInfo info;
    CHECK(!P1::DecodeChatNodeInfo("not-json", info));
    CHECK(!P1::DecodeChatNodeInfo("[]", info));
}

TESTFW_TEST(BuildRpcEndpoint_joins_host_and_port)
{
    CHECK_EQ(P1::BuildRpcEndpoint(SampleNode()), std::string("10.0.0.11:50081"));
    ChatNodeInfo empty;
    CHECK_EQ(P1::BuildRpcEndpoint(empty), std::string(""));
}
