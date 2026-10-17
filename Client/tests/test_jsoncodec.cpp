#include <QtTest>

#include "constants.h"
#include "domainmodels.h"
#include "jsoncodec.h"

// JSON 编解码层：请求序列化 → 解析回模型的往返，以及「字段缺失/非法」时的失败语义。
// 这一层不依赖网络与 UI，是客户端所有业务消息的解析入口。

namespace {

QByteArray Json(const QString &text)
{
    return text.toUtf8();
}

} // namespace

class TestJsonCodec : public QObject
{
    Q_OBJECT

private slots:
    void extractErrorSemantics();
    void searchRspParse();
    void searchRspRejectsErrorAndGarbage();
    void notifyAddFriendReqFields();
    void notifyAuthFriendActionCompatibility();
    void authFriendRspCarriesProfileOnlyWhenAccepted();
    void notifyTextChatMsgOrderAndFields();
    void simpleResponsesOnlyCheckError();
    void loginHttpRspFields();
    void applyAndFriendListFields();
    void serializersRoundTrip();
};

void TestJsonCodec::extractErrorSemantics()
{
    // 缺 error 字段 = 解析失败（历史缺陷：被当成 error=0「成功」）
    QCOMPARE(ExtractError(QJsonObject()), ErrorCodes::JSON_PARSE_ERROR);

    QJsonObject ok;
    ok["error"] = 0;
    QCOMPARE(ExtractError(ok), ErrorCodes::SUCCESS);

    QJsonObject emailNotMatch;
    emailNotMatch["error"] = static_cast<int>(ErrorCodes::EMAIL_NOT_MATCH);
    QCOMPARE(ExtractError(emailNotMatch), ErrorCodes::EMAIL_NOT_MATCH);
}

void TestJsonCodec::searchRspParse()
{
    const QByteArray data = Json(R"({"error":0,"uid":1001,"name":"alice","nick":"小A",)"
                                 R"("desc":"hello","sex":1,"icon":"head_2"})");
    const auto info = JsonParser::ParseSearchRsp(data);
    QVERIFY(info != nullptr);
    QCOMPARE(info->uid_, 1001);
    QCOMPARE(info->name_, QString("alice"));
    QCOMPARE(info->nick_, QString("小A"));
    QCOMPARE(info->desc_, QString("hello"));
    QCOMPARE(info->sex_, 1);
    QCOMPARE(info->icon_, QString("head_2"));
}

void TestJsonCodec::searchRspRejectsErrorAndGarbage()
{
    QVERIFY(JsonParser::ParseSearchRsp(Json(R"({"error":8})")) == nullptr);
    QVERIFY(JsonParser::ParseSearchRsp(Json(R"({"uid":1,"name":"a"})")) == nullptr); // 缺 error
    QVERIFY(JsonParser::ParseSearchRsp(Json("not-a-json")) == nullptr);
    QVERIFY(JsonParser::ParseSearchRsp(QByteArray()) == nullptr);
}

void TestJsonCodec::notifyAddFriendReqFields()
{
    const QByteArray data = Json(R"({"error":0,"apply_uid":2002,"name":"bob","desc":"想加你",)"
                                 R"("icon":"head_3","nick":"小B","sex":2})");
    const auto apply = JsonParser::ParseNotifyAddFriendReq(data);
    QVERIFY(apply != nullptr);
    QCOMPARE(apply->uid_, 2002); // apply_uid 映射到基类 uid_
    QCOMPARE(apply->name_, QString("bob"));
    QCOMPARE(apply->desc_, QString("想加你"));
    QCOMPARE(apply->icon_, QString("head_3"));
    QCOMPARE(apply->nick_, QString("小B"));
    QCOMPARE(apply->sex_, 2);

    QVERIFY(JsonParser::ParseNotifyAddFriendReq(Json(R"({"apply_uid":2002})")) == nullptr);
}

void TestJsonCodec::notifyAuthFriendActionCompatibility()
{
    // 显式拒绝：不带对方资料
    const auto rejected = JsonParser::ParseNotifyAuthFriendReq(
        Json(R"({"error":0,"action":2,"from_uid":3003,"name":"carol","nick":"小C","icon":"head_1","sex":0})"));
    QCOMPARE(rejected.error, ErrorCodes::SUCCESS);
    QCOMPARE(rejected.action, AcceptAction::REJECT);
    QVERIFY(rejected.info == nullptr);

    // 同意：带对方资料
    const auto accepted = JsonParser::ParseNotifyAuthFriendReq(
        Json(R"({"error":0,"action":1,"from_uid":3003,"name":"carol","nick":"小C","icon":"head_1","sex":0})"));
    QCOMPARE(accepted.action, AcceptAction::ACCEPT);
    QVERIFY(accepted.info != nullptr);
    QCOMPARE(accepted.info->uid_, 3003);
    QCOMPARE(accepted.info->name_, QString("carol"));

    // 旧协议兼容：不带 action / action=0 / 未知值 → 按同意处理（与服务端 ResolvePeerAuthAction 一致），
    // 否则旧消息会被客户端误显示成「对方拒绝」
    for (const QString &body : {R"({"error":0,"from_uid":3003})", R"({"error":0,"action":0,"from_uid":3003})",
                                R"({"error":0,"action":9,"from_uid":3003})"}) {
        const auto result = JsonParser::ParseNotifyAuthFriendReq(Json(body));
        QCOMPARE(result.action, AcceptAction::ACCEPT);
        QVERIFY(result.info != nullptr);
    }

    // 回包错误码照原样带出，不误判成同意
    const auto failed = JsonParser::ParseNotifyAuthFriendReq(Json(R"({"error":1,"action":1})"));
    QCOMPARE(failed.error, ErrorCodes::ERROR_JSON);
    QVERIFY(failed.info == nullptr);
}

void TestJsonCodec::authFriendRspCarriesProfileOnlyWhenAccepted()
{
    const auto accepted = JsonParser::ParseAuthFriendRsp(
        Json(R"({"error":0,"action":1,"uid":4004,"name":"dave","nick":"小D","icon":"head_4","sex":1})"));
    QCOMPARE(accepted.action, AcceptAction::ACCEPT);
    QVERIFY(accepted.info != nullptr);
    QCOMPARE(accepted.info->uid_, 4004);
    QCOMPARE(accepted.info->icon_, QString("head_4"));

    const auto rejected = JsonParser::ParseAuthFriendRsp(Json(R"({"error":0,"action":2,"uid":4004})"));
    QCOMPARE(rejected.action, AcceptAction::REJECT);
    QVERIFY(rejected.info == nullptr);
}

void TestJsonCodec::notifyTextChatMsgOrderAndFields()
{
    const QByteArray data = Json(R"({"error":0,"from_uid":11,"to_uid":22,"text_array":[)"
                                 R"({"content":"第一条","msg_id":"id-1"},)"
                                 R"({"content":"second","msg_id":"id-2"}]})");
    const auto result = JsonParser::ParseNotifyTextChatMsg(data);
    QCOMPARE(result.error, ErrorCodes::SUCCESS);
    QCOMPARE(result.fromUid, 11);
    QCOMPARE(result.toUid, 22);
    QCOMPARE(static_cast<int>(result.msgs.size()), 2);
    QCOMPARE(result.msgs[0]->msg_id_, QString("id-1"));
    QCOMPARE(result.msgs[0]->msg_content_, QString("第一条"));
    QCOMPARE(result.msgs[0]->from_uid_, 11);
    QCOMPARE(result.msgs[0]->to_uid_, 22);
    QCOMPARE(result.msgs[1]->msg_id_, QString("id-2"));
    QCOMPARE(result.msgs[1]->msg_content_, QString("second"));

    // 缺 text_array 时消息列表为空而不是崩溃
    const auto noArray = JsonParser::ParseNotifyTextChatMsg(Json(R"({"error":0,"from_uid":11,"to_uid":22})"));
    QCOMPARE(noArray.error, ErrorCodes::SUCCESS);
    QVERIFY(noArray.msgs.empty());
}

void TestJsonCodec::simpleResponsesOnlyCheckError()
{
    QCOMPARE(JsonParser::ParseAddFriendRsp(Json(R"({"error":0})")), ErrorCodes::SUCCESS);
    QCOMPARE(JsonParser::ParseAddFriendRsp(Json(R"({"error":8})")), ErrorCodes::UID_INVALID);
    QCOMPARE(JsonParser::ParseAddFriendRsp(Json("bad")), ErrorCodes::JSON_PARSE_ERROR);
    QCOMPARE(JsonParser::ParseTextChatMsgRsp(Json(R"({"error":1})")), ErrorCodes::ERROR_JSON);
    QCOMPARE(JsonParser::ParseTextChatMsgRsp(Json("{}")), ErrorCodes::JSON_PARSE_ERROR);
}

void TestJsonCodec::loginHttpRspFields()
{
    const QJsonObject obj = QJsonDocument::fromJson(Json(R"({"error":0,"uid":7,"host":"127.0.0.1",)"
                                                        R"("port":50061,"token":"tok-abc"})"))
                                .object();
    const ServerInfo info = JsonParser::ParseLoginHttpRsp(obj);
    QCOMPARE(info.uid, 7);
    QCOMPARE(info.host, QString("127.0.0.1"));
    QCOMPARE(info.port, quint16(50061));
    QCOMPARE(info.token, QString("tok-abc"));
}

void TestJsonCodec::applyAndFriendListFields()
{
    const QJsonArray applyArray = QJsonDocument::fromJson(Json(R"([{"uid":5,"name":"eve","desc":"备注",)"
                                                               R"("icon":"head_5","nick":"小E","sex":1,"status":0}])"))
                                      .array();
    const auto applies = JsonParser::ParseApplyList(applyArray);
    QCOMPARE(static_cast<int>(applies.size()), 1);
    QCOMPARE(applies[0]->uid_, 5);
    QCOMPARE(applies[0]->name_, QString("eve"));
    QCOMPARE(applies[0]->desc_, QString("备注"));
    QCOMPARE(applies[0]->sex_, 1);
    QCOMPARE(applies[0]->status_, 0);

    const QJsonArray friendArray =
        QJsonDocument::fromJson(Json(R"([{"uid":6,"name":"frank","nick":"小F","icon":"head_1","sex":2,"desc":"签名"}])"))
            .array();
    const auto friends = JsonParser::ParseFriendList(friendArray);
    QCOMPARE(static_cast<int>(friends.size()), 1);
    QCOMPARE(friends[0]->uid_, 6);
    QCOMPARE(friends[0]->nick_, QString("小F"));
    QCOMPARE(friends[0]->desc_, QString("签名"));
    QVERIFY(friends[0]->chat_msgs_.empty());
    QVERIFY(friends[0]->last_msg_.isEmpty());

    QVERIFY(JsonParser::ParseApplyList(QJsonArray()).empty());
    QVERIFY(JsonParser::ParseFriendList(QJsonArray()).empty());
}

void TestJsonCodec::serializersRoundTrip()
{
    const QJsonObject loginReq =
        QJsonDocument::fromJson(JsonSerializer::SerializeChatLoginReq(12, "tok-1")).object();
    QCOMPARE(loginReq["uid"].toInt(), 12);
    QCOMPARE(loginReq["token"].toString(), QString("tok-1"));

    const QJsonObject searchReq = QJsonDocument::fromJson(JsonSerializer::SerializeSearchUserReq("1001")).object();
    QCOMPARE(searchReq["uid"].toString(), QString("1001")); // 协议里是字符串

    const QJsonObject addReq =
        QJsonDocument::fromJson(JsonSerializer::SerializeAddFriendReq(1, 2, "n", "d", "remark")).object();
    QCOMPARE(addReq["from_uid"].toInt(), 1);
    QCOMPARE(addReq["to_uid"].toInt(), 2);
    QCOMPARE(addReq["name"].toString(), QString("n"));
    QCOMPARE(addReq["desc"].toString(), QString("d"));
    QCOMPARE(addReq["remark_name"].toString(), QString("remark"));
    QVERIFY(!addReq.contains("action")); // 加好友请求不带 action

    const QJsonObject authReq =
        QJsonDocument::fromJson(JsonSerializer::SerializeAuthFriendReq(1, 2, "n", "d", "r", AcceptAction::REJECT))
            .object();
    QCOMPARE(authReq["action"].toInt(), AcceptAction::REJECT);

    QJsonArray textArray;
    QJsonObject first;
    first["content"] = "hi";
    first["msg_id"] = "id-1";
    textArray.append(first);
    QJsonObject second;
    second["content"] = "yo";
    second["msg_id"] = "id-2";
    textArray.append(second);
    const QJsonObject textReq =
        QJsonDocument::fromJson(JsonSerializer::SerializeTextChatMsgReq(1, 2, textArray)).object();
    QCOMPARE(textReq["from_uid"].toInt(), 1);
    QCOMPARE(textReq["to_uid"].toInt(), 2);
    QCOMPARE(textReq["text_array"].toArray().size(), 2);
    QCOMPARE(textReq["text_array"].toArray().at(1).toObject()["msg_id"].toString(), QString("id-2"));
}

QTEST_GUILESS_MAIN(TestJsonCodec)
#include "test_jsoncodec.moc"
