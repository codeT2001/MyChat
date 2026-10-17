#include "test_framework.h"

#include <cstdint>
#include <string>

#include "msg_node.h"

using P1::MsgNode;

TESTFW_TEST(MsgNode_ConstructsEmpty)
{
    MsgNode node(16, 10001);
    CHECK_EQ(node.GetMsgId(), static_cast<uint16_t>(10001));
    CHECK_EQ(node.GetCurLen(), static_cast<uint16_t>(0));
    CHECK(node.GetData() != nullptr);
}

TESTFW_TEST(MsgNode_SetCurLen_StoresWithinCapacity)
{
    MsgNode node(16);
    node.SetCurLen(10);
    CHECK_EQ(node.GetCurLen(), static_cast<uint16_t>(10));
}

TESTFW_TEST(MsgNode_SetCurLen_ClampsToCapacity)
{
    MsgNode node(16);
    node.SetCurLen(999);
    CHECK_EQ(node.GetCurLen(), static_cast<uint16_t>(16));
}

TESTFW_TEST(MsgNode_DataIsWritableAndReadable)
{
    MsgNode node(8, 7);
    const std::string payload = "hello";
    std::string copy(payload);
    for (size_t i = 0; i < copy.size(); ++i) {
        node.GetData()[i] = copy[i];
    }
    node.SetCurLen(static_cast<uint16_t>(copy.size()));
    CHECK_EQ(std::string(node.GetData(), node.GetCurLen()), payload);
}
