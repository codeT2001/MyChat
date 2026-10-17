#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "msg_codec.h"

namespace {

using P1::DecodeMsgHead;
using P1::EncodeMsgHead;
using P1::IsValidMsgBodySize;
using P1::ReadUint16BigEndian;
using P1::ReadUint32BigEndian;
using P1::WriteUint16BigEndian;
using P1::WriteUint32BigEndian;

// —— 帧头布局常量（与客户端 QDataStream 大端读写对齐）——
TESTFW_TEST(MsgHead_layout_is_msgid2_plus_len4)
{
    CHECK_EQ(P1::MSG_HEAD_ID_LENGTH, 2u);
    CHECK_EQ(P1::MSG_HEAD_DATA_LENGTH, 4u);
    CHECK_EQ(P1::MSG_HEAD_TOTAL_LENGTH, 6u);
}

// —— 字节序：必须是网络字节序（大端）——
TESTFW_TEST(MsgHead_encodes_big_endian_bytes)
{
    unsigned char buf[P1::MSG_HEAD_TOTAL_LENGTH];
    std::memset(buf, 0, sizeof(buf));
    EncodeMsgHead(buf, 0x0102, 0x01020304u);

    CHECK_EQ(static_cast<int>(buf[0]), 0x01);   // msgId 高位在前
    CHECK_EQ(static_cast<int>(buf[1]), 0x02);
    CHECK_EQ(static_cast<int>(buf[2]), 0x01);   // 长度高位在前
    CHECK_EQ(static_cast<int>(buf[3]), 0x02);
    CHECK_EQ(static_cast<int>(buf[4]), 0x03);
    CHECK_EQ(static_cast<int>(buf[5]), 0x04);
}

TESTFW_TEST(MsgHead_round_trip)
{
    const uint16_t ids[] = { 0, 1, 1001, 65535 };
    const uint32_t sizes[] = { 0, 1, 255, 2048, 65535 };
    for (const uint16_t id : ids) {
        for (const uint32_t size : sizes) {
            unsigned char buf[P1::MSG_HEAD_TOTAL_LENGTH];
            EncodeMsgHead(buf, id, size);
            uint16_t gotId = 0;
            uint32_t gotSize = 0;
            DecodeMsgHead(buf, gotId, gotSize);
            CHECK_EQ(gotId, id);
            CHECK_EQ(gotSize, size);
        }
    }
}

TESTFW_TEST(Uint16_big_endian_helpers)
{
    unsigned char buf[2];
    WriteUint16BigEndian(buf, 0x1234);
    CHECK_EQ(static_cast<int>(buf[0]), 0x12);
    CHECK_EQ(static_cast<int>(buf[1]), 0x34);
    CHECK_EQ(ReadUint16BigEndian(buf), static_cast<uint16_t>(0x1234));
}

TESTFW_TEST(Uint32_big_endian_helpers)
{
    unsigned char buf[4];
    WriteUint32BigEndian(buf, 0xDEADBEEFu);
    CHECK_EQ(static_cast<int>(buf[0]), 0xDE);
    CHECK_EQ(static_cast<int>(buf[1]), 0xAD);
    CHECK_EQ(static_cast<int>(buf[2]), 0xBE);
    CHECK_EQ(static_cast<int>(buf[3]), 0xEF);
    CHECK_EQ(ReadUint32BigEndian(buf), 0xDEADBEEFu);
}

// —— body 长度校验 ——
TESTFW_TEST(MsgBodySize_boundary)
{
    CHECK(IsValidMsgBodySize(0));
    CHECK(IsValidMsgBodySize(P1::MAX_MSG_LENGTH));          // 上界本身合法
    CHECK(!IsValidMsgBodySize(P1::MAX_MSG_LENGTH + 1));     // 超 1 字节即非法
    CHECK(!IsValidMsgBodySize(0xFFFFFFFFu));                // 恶意长度不能溢出
}

// —— 与发送路径一致：先把 head 编码，再把 body 拼上，整帧可还原 ——
TESTFW_TEST(MsgFrame_head_plus_body_is_reassemblable)
{
    const std::string body = "{\"uid\":1001}";
    std::string frame;
    frame.resize(P1::MSG_HEAD_TOTAL_LENGTH);
    EncodeMsgHead(&frame[0], 1001, static_cast<uint32_t>(body.size()));
    frame += body;

    uint16_t msgId = 0;
    uint32_t bodySize = 0;
    DecodeMsgHead(frame.data(), msgId, bodySize);
    CHECK_EQ(msgId, static_cast<uint16_t>(1001));
    CHECK_EQ(bodySize, static_cast<uint32_t>(body.size()));
    CHECK_EQ(frame.size(), P1::MSG_HEAD_TOTAL_LENGTH + body.size());
    CHECK_EQ(frame.substr(P1::MSG_HEAD_TOTAL_LENGTH), body);
}

} // namespace
