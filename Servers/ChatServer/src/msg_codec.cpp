#include "msg_codec.h"

#include <arpa/inet.h>
#include <cstring>

namespace P1 {

void WriteUint16BigEndian(void* dest, uint16_t value)
{
    const uint16_t netVal = htons(value);
    std::memcpy(dest, &netVal, sizeof(netVal));
}

uint16_t ReadUint16BigEndian(const void* src)
{
    uint16_t val = 0;
    std::memcpy(&val, src, sizeof(val));
    return ntohs(val);
}

void WriteUint32BigEndian(void* dest, uint32_t value)
{
    const uint32_t netVal = htonl(value);
    std::memcpy(dest, &netVal, sizeof(netVal));
}

uint32_t ReadUint32BigEndian(const void* src)
{
    uint32_t val = 0;
    std::memcpy(&val, src, sizeof(val));
    return ntohl(val);
}

void EncodeMsgHead(void* dest, uint16_t msgId, uint32_t bodySize)
{
    WriteUint16BigEndian(dest, msgId);
    WriteUint32BigEndian(static_cast<char*>(dest) + MSG_HEAD_ID_LENGTH, bodySize);
}

void DecodeMsgHead(const void* src, uint16_t& msgId, uint32_t& bodySize)
{
    msgId = ReadUint16BigEndian(src);
    bodySize = ReadUint32BigEndian(static_cast<const char*>(src) + MSG_HEAD_ID_LENGTH);
}

bool IsValidMsgBodySize(uint32_t bodySize)
{
    return bodySize <= MAX_MSG_LENGTH;
}

bool IsValidOutboundMsgBodySize(uint32_t bodySize)
{
    return bodySize <= MAX_OUTBOUND_MSG_LENGTH;
}

} // namespace P1
