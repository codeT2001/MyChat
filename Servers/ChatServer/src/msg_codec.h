#ifndef _P1_MSG_CODEC_H_
#define _P1_MSG_CODEC_H_

#include <cstdint>

namespace P1 {

// TCP 帧头布局：msgId(2B) + body 长度(4B)，多字节整数一律网络字节序（大端）。
// 与客户端一致：Client/src/tcpmanager.cpp 用 QDataStream（默认大端）读写 uint16_t/uint32_t。
constexpr uint32_t MSG_HEAD_TOTAL_LENGTH = 6;
constexpr uint32_t MSG_HEAD_ID_LENGTH = 2;
constexpr uint32_t MSG_HEAD_DATA_LENGTH = 4;

// 帧头布局一旦被改动，下面的断言会在编译期报错，避免收发两端错位
static_assert(MSG_HEAD_ID_LENGTH + MSG_HEAD_DATA_LENGTH == MSG_HEAD_TOTAL_LENGTH,
    "TCP frame header layout mismatch");

// 单条消息 body 上限（2KB）：超过即视为非法帧，服务端主动断开
constexpr uint32_t MAX_MSG_LENGTH = 1024 * 2;

void WriteUint16BigEndian(void* dest, uint16_t value);
uint16_t ReadUint16BigEndian(const void* src);
void WriteUint32BigEndian(void* dest, uint32_t value);
uint32_t ReadUint32BigEndian(const void* src);

// 写入 6 字节帧头：msgId(2B, 大端) + bodySize(4B, 大端)
void EncodeMsgHead(void* dest, uint16_t msgId, uint32_t bodySize);

// 解析帧头，拆出 msgId 与 body 长度
void DecodeMsgHead(const void* src, uint16_t& msgId, uint32_t& bodySize);

// body 长度是否在允许范围内
bool IsValidMsgBodySize(uint32_t bodySize);

} // namespace P1

#endif // _P1_MSG_CODEC_H_
