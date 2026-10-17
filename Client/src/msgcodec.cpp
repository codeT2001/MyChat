#include "msgcodec.h"

#include <QDataStream>
#include <QIODevice>

namespace MsgCodec {

QByteArray EncodeFrame(uint16_t msgId, const QByteArray &body)
{
    QByteArray block;
    block.reserve(static_cast<int>(HEAD_TOTAL_LENGTH) + body.size());
    QDataStream out(&block, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian); // 与服务端 htons/htonl 对应
    out << msgId << static_cast<uint32_t>(body.size());
    block.append(body);
    return block;
}

void DecodeMsgHead(const QByteArray &head, uint16_t &msgId, uint32_t &bodyLen)
{
    QDataStream in(head);
    in.setByteOrder(QDataStream::BigEndian);
    in >> msgId >> bodyLen;
}

bool IsAcceptableBodySize(uint32_t bodyLen)
{
    return bodyLen <= MAX_RECV_BODY;
}

} // namespace MsgCodec
