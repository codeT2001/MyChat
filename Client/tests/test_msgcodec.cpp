#include <QtTest>

#include "constants.h"
#include "msgcodec.h"
#include "test_helpers.h"

// TCP 帧编解码：字节序是「服务端 ↔ Qt 客户端」最容易悄无声息错位的契约
// （客户端 QDataStream 与服务端 htons/htonl 必须都是大端），这里逐字节断言。

class TestMsgCodec : public QObject
{
    Q_OBJECT

private slots:
    void headerLayoutConstants();
    void encodeFrameWritesBigEndianHead();
    void encodeFrameWritesFullLengthBigEndian();
    void decodeHeadRoundTrip();
    void decodeHeadHandlesBoundaryIds();
    void acceptableBodySizeBoundaries();
    void limitsMatchServerHeader();
    void textBatchFitsServerFrameLimit();
};

void TestMsgCodec::headerLayoutConstants()
{
    QCOMPARE(MsgCodec::HEAD_ID_LENGTH, 2u);
    QCOMPARE(MsgCodec::HEAD_DATA_LENGTH, 4u);
    QCOMPARE(MsgCodec::HEAD_TOTAL_LENGTH, 6u);
}

void TestMsgCodec::encodeFrameWritesBigEndianHead()
{
    const QByteArray frame = MsgCodec::EncodeFrame(0x0102, QByteArray("abc"));
    QCOMPARE(frame.size(), 6 + 3);
    // msgId = 0x0102（大端：高字节在前）
    QCOMPARE(static_cast<quint8>(frame.at(0)), quint8(0x01));
    QCOMPARE(static_cast<quint8>(frame.at(1)), quint8(0x02));
    // body 长度 = 3（大端）
    QCOMPARE(static_cast<quint8>(frame.at(2)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(frame.at(3)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(frame.at(4)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(frame.at(5)), quint8(0x03));
    QCOMPARE(frame.mid(6), QByteArray("abc"));
}

void TestMsgCodec::encodeFrameWritesFullLengthBigEndian()
{
    // 长度字段要占满 4 字节：0x00010203 逐字节断言
    const QByteArray body(0x00010203, 'x');
    const QByteArray frame = MsgCodec::EncodeFrame(0xFFFF, body);
    QCOMPARE(static_cast<quint8>(frame.at(0)), quint8(0xFF));
    QCOMPARE(static_cast<quint8>(frame.at(1)), quint8(0xFF));
    QCOMPARE(static_cast<quint8>(frame.at(2)), quint8(0x00));
    QCOMPARE(static_cast<quint8>(frame.at(3)), quint8(0x01));
    QCOMPARE(static_cast<quint8>(frame.at(4)), quint8(0x02));
    QCOMPARE(static_cast<quint8>(frame.at(5)), quint8(0x03));
    QCOMPARE(frame.size(), 6 + body.size());
}

void TestMsgCodec::decodeHeadRoundTrip()
{
    const QByteArray body("{\"error\":0}");
    const QByteArray frame = MsgCodec::EncodeFrame(static_cast<uint16_t>(RequestId::TEXT_CHAT_MSG_RSP), body);

    uint16_t msgId = 0;
    uint32_t bodyLen = 0;
    MsgCodec::DecodeMsgHead(frame, msgId, bodyLen);
    QCOMPARE(msgId, static_cast<uint16_t>(RequestId::TEXT_CHAT_MSG_RSP));
    QCOMPARE(bodyLen, static_cast<uint32_t>(body.size()));
    QCOMPARE(frame.mid(static_cast<int>(MsgCodec::HEAD_TOTAL_LENGTH), static_cast<int>(bodyLen)), body);
}

void TestMsgCodec::decodeHeadHandlesBoundaryIds()
{
    for (const uint16_t id : {uint16_t(0), uint16_t(1), uint16_t(0x7FFF), uint16_t(0xFFFF)}) {
        uint16_t msgId = 123;
        uint32_t bodyLen = 123;
        MsgCodec::DecodeMsgHead(MsgCodec::EncodeFrame(id, QByteArray()), msgId, bodyLen);
        QCOMPARE(msgId, id);
        QCOMPARE(bodyLen, 0u); // 心跳等空 body 帧
    }
    // 心跳消息号是 99999，落在 uint16 范围内，帧头不截断
    QCOMPARE(static_cast<uint16_t>(RequestId::CHAT_HEARTBEAT), uint16_t(99999));
}

void TestMsgCodec::acceptableBodySizeBoundaries()
{
    QVERIFY(MsgCodec::IsAcceptableBodySize(0));
    QVERIFY(MsgCodec::IsAcceptableBodySize(MsgCodec::MAX_SERVER_FRAME_BODY));
    QVERIFY(MsgCodec::IsAcceptableBodySize(MsgCodec::MAX_RECV_BODY));
    QVERIFY(!MsgCodec::IsAcceptableBodySize(MsgCodec::MAX_RECV_BODY + 1));
    QVERIFY(!MsgCodec::IsAcceptableBodySize(0xFFFFFFFFu)); // 不因溢出而放行
}

void TestMsgCodec::limitsMatchServerHeader()
{
    const QString source = testhelp::readRepoFile("Servers/ChatServer/src/msg_codec.h");
    QVERIFY2(!source.isEmpty(), "read Servers/ChatServer/src/msg_codec.h failed (check CHAT_REPO_ROOT)");

    QCOMPARE(MsgCodec::HEAD_TOTAL_LENGTH,
             static_cast<uint32_t>(testhelp::symbolValue(source, "MSG_HEAD_TOTAL_LENGTH")));
    QCOMPARE(MsgCodec::HEAD_ID_LENGTH,
             static_cast<uint32_t>(testhelp::symbolValue(source, "MSG_HEAD_ID_LENGTH")));
    QCOMPARE(MsgCodec::HEAD_DATA_LENGTH,
             static_cast<uint32_t>(testhelp::symbolValue(source, "MSG_HEAD_DATA_LENGTH")));

    // 服务端单帧 body 上限（MAX_MSG_LENGTH = 1024*2）必须与客户端记录的一致，
    // 否则客户端会发出服务端直接断连的帧
    const int serverMax = testhelp::symbolValue(source, "MAX_MSG_LENGTH");
    QVERIFY(serverMax > 0);
    QCOMPARE(MsgCodec::MAX_SERVER_FRAME_BODY, static_cast<uint32_t>(serverMax));
    QCOMPARE(TextMsgLimits::MAX_FRAME_BODY_BYTES, serverMax);
}

void TestMsgCodec::textBatchFitsServerFrameLimit()
{
    // 客户端的单批/单条上限必须留出包装余量，保证「内容 + JSON 包装」不超过服务端单帧上限
    QVERIFY(TextMsgLimits::MAX_SINGLE_MSG_BYTES <= TextMsgLimits::MAX_TEXT_BATCH_BYTES);
    QVERIFY(TextMsgLimits::MAX_SINGLE_MSG_BYTES + TextMsgLimits::PER_MSG_JSON_OVERHEAD_BYTES +
                TextMsgLimits::BATCH_JSON_OVERHEAD_BYTES <=
            TextMsgLimits::MAX_FRAME_BODY_BYTES);
    QVERIFY(TextMsgLimits::MAX_TEXT_BATCH_BYTES + TextMsgLimits::BATCH_JSON_OVERHEAD_BYTES <=
            TextMsgLimits::MAX_FRAME_BODY_BYTES);
    // 包装开销不能离谱（否则 API 语义会悄悄变成「几乎每条都单独发一批」）
    QVERIFY(TextMsgLimits::PER_MSG_JSON_OVERHEAD_BYTES < TextMsgLimits::MAX_SINGLE_MSG_BYTES);
}

QTEST_GUILESS_MAIN(TestMsgCodec)
#include "test_msgcodec.moc"
