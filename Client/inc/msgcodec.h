#ifndef MSGCODEC_H
#define MSGCODEC_H

#include <QByteArray>
#include <cstdint>

// 客户端侧 TCP 帧编解码：与服务端 Servers/ChatServer/src/msg_codec.h 是同一份协议契约。
//
// 帧格式（多字节整数一律网络字节序/大端）：
//   msgId(2B) + body 长度(4B) + body(JSON, UTF-8)
//
// HTTP/UI 层不依赖本模块；TcpManager 收发、测试与上限断言都从这里取值。
namespace MsgCodec {

constexpr uint32_t HEAD_ID_LENGTH = 2;
constexpr uint32_t HEAD_DATA_LENGTH = 4;
constexpr uint32_t HEAD_TOTAL_LENGTH = HEAD_ID_LENGTH + HEAD_DATA_LENGTH;

// 帧头布局一旦被改动，下面的断言会在编译期报错，避免与服务端/历史报文错位
static_assert(HEAD_ID_LENGTH + HEAD_DATA_LENGTH == HEAD_TOTAL_LENGTH, "TCP frame header layout mismatch");

// 服务端单帧 body 硬上限（ChatServer MAX_MSG_LENGTH = 1024 * 2）：
// 超过即被服务端视为非法帧并断开连接，客户端发送侧必须先分批
constexpr uint32_t MAX_SERVER_FRAME_BODY = 1024 * 2;

// 接收侧保险上限：防御异常/恶意对端撑爆内存（正常服务端不会发超过 MAX_SERVER_FRAME_BODY 的帧）
constexpr uint32_t MAX_RECV_BODY = 10 * 1024 * 1024;
// 接收缓冲区上限（未解析完的残包累计超过即断开）
constexpr uint32_t MAX_RECV_BUFFER = 1024 * 1024;

// 组装完整帧（6 字节帧头 + body）
QByteArray EncodeFrame(uint16_t msgId, const QByteArray &body);

// 解析帧头：要求 head.size() >= HEAD_TOTAL_LENGTH
void DecodeMsgHead(const QByteArray &head, uint16_t &msgId, uint32_t &bodyLen);

// body 长度是否在客户端可接受范围内
bool IsAcceptableBodySize(uint32_t bodyLen);

} // namespace MsgCodec

#endif // MSGCODEC_H
