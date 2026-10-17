#include <QtTest>
#include <QDir>
#include <QSet>

#include "constants.h"
#include "test_helpers.h"

// 跨项目协议契约：客户端与服务端各有一份枚举，协议载荷里只有数字，
// 一旦有人只改一侧，客户端就会把服务端错误码解释成别的含义。
// 这里把「必须同值」这件事变成可执行的测试（服务端 constants.h 为权威定义）。

namespace {

struct CodePair {
    const char *name;
    int clientValue;
};

bool serverSourceReady(QString &source)
{
    source = testhelp::readRepoFile("Servers/common/include/chat/constants.h");
    return !source.isEmpty();
}

} // namespace

class TestProtocolContract : public QObject
{
    Q_OBJECT

private slots:
    void requestIdsMatchServer();
    void errorCodesMatchServer();
    void clientLocalCodesAvoidServerRange();
    void acceptActionsMatchServer();
    void httpPathsMatchGateServerRoutes();
    void avatarCountMatchesHeadImages();
};

void TestProtocolContract::requestIdsMatchServer()
{
    QString server;
    QVERIFY2(serverSourceReady(server), "读不到服务端 constants.h（检查 CHAT_REPO_ROOT）");

    const CodePair pairs[] = {
        {"CHAT_LOGIN_REQ", static_cast<int>(RequestId::CHAT_LOGIN_REQ)},
        {"CHAT_LOGIN_RSP", static_cast<int>(RequestId::CHAT_LOGIN_RSP)},
        {"SEARCH_USER_REQ", static_cast<int>(RequestId::SEARCH_USER_REQ)},
        {"SEARCH_USER_RSP", static_cast<int>(RequestId::SEARCH_USER_RSP)},
        {"ADD_FRIEND_REQ", static_cast<int>(RequestId::ADD_FRIEND_REQ)},
        {"ADD_FRIEND_RSP", static_cast<int>(RequestId::ADD_FRIEND_RSP)},
        {"NOTIFY_ADD_FRIEND_REQ", static_cast<int>(RequestId::NOTIFY_ADD_FRIEND_REQ)},
        {"NOTIFY_ADD_FRIEND_RSP", static_cast<int>(RequestId::NOTIFY_ADD_FRIEND_RSP)},
        {"AUTH_FRIEND_REQ", static_cast<int>(RequestId::AUTH_FRIEND_REQ)},
        {"AUTH_FRIEND_RSP", static_cast<int>(RequestId::AUTH_FRIEND_RSP)},
        {"NOTIFY_AUTH_FRIEND_REQ", static_cast<int>(RequestId::NOTIFY_AUTH_FRIEND_REQ)},
        {"NOTIFY_AUTH_FRIEND_RSP", static_cast<int>(RequestId::NOTIFY_AUTH_FRIEND_RSP)},
        {"TEXT_CHAT_MSG_REQ", static_cast<int>(RequestId::TEXT_CHAT_MSG_REQ)},
        {"TEXT_CHAT_MSG_RSP", static_cast<int>(RequestId::TEXT_CHAT_MSG_RSP)},
        {"NOTIFY_TEXT_CHAT_MSG_REQ", static_cast<int>(RequestId::NOTIFY_TEXT_CHAT_MSG_REQ)},
        {"NOTIFY_TEXT_CHAT_MSG_RSP", static_cast<int>(RequestId::NOTIFY_TEXT_CHAT_MSG_RSP)},
        {"NOTIFY_KICK", static_cast<int>(RequestId::NOTIFY_KICK)},
        {"CHAT_HEARTBEAT", static_cast<int>(RequestId::CHAT_HEARTBEAT)},
    };

    for (const CodePair &pair : pairs) {
        const int serverValue = testhelp::symbolValue(server, QString::fromLatin1(pair.name));
        QVERIFY2(serverValue != INT_MIN,
                 qPrintable(QString("服务端 constants.h 里找不到 %1").arg(pair.name)));
        QVERIFY2(serverValue == pair.clientValue,
                 qPrintable(QString("消息号 %1 不一致：客户端=%2，服务端=%3")
                                .arg(pair.name)
                                .arg(pair.clientValue)
                                .arg(serverValue)));
    }
}

void TestProtocolContract::errorCodesMatchServer()
{
    QString server;
    QVERIFY2(serverSourceReady(server), "读不到服务端 constants.h（检查 CHAT_REPO_ROOT）");

    // 只列两侧都应存在的通用码；客户端本地码另有断言（clientLocalCodesAvoidServerRange）
    const CodePair pairs[] = {
        {"SUCCESS", static_cast<int>(ErrorCodes::SUCCESS)},
        {"ERROR_JSON", static_cast<int>(ErrorCodes::ERROR_JSON)},
        {"RPC_FAILED", static_cast<int>(ErrorCodes::RPC_FAILED)},
        {"VERIFYCODE_NOT_FOUND_OR_EXPIRED", static_cast<int>(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED)},
        {"USER_EXIST", static_cast<int>(ErrorCodes::USER_EXIST)},
        {"EMAIL_NOT_MATCH", static_cast<int>(ErrorCodes::EMAIL_NOT_MATCH)},
        {"UPDATE_PASSWORD_FAILED", static_cast<int>(ErrorCodes::UPDATE_PASSWORD_FAILED)},
        {"PASSWORD_NOT_MATCH", static_cast<int>(ErrorCodes::PASSWORD_NOT_MATCH)},
        {"UID_INVALID", static_cast<int>(ErrorCodes::UID_INVALID)},
        {"TOKEN_INVALID", static_cast<int>(ErrorCodes::TOKEN_INVALID)},
        {"USER_ALREADY_LOGIN", static_cast<int>(ErrorCodes::USER_ALREADY_LOGIN)},
        {"VERIFY_CODE_EXISTS", static_cast<int>(ErrorCodes::VERIFY_CODE_EXISTS)},
        {"VERIFY_REDIS_ERROR", static_cast<int>(ErrorCodes::VERIFY_REDIS_ERROR)},
        {"VERIFY_MAIL_FAILED", static_cast<int>(ErrorCodes::VERIFY_MAIL_FAILED)},
    };

    for (const CodePair &pair : pairs) {
        const int serverValue = testhelp::symbolValue(server, QString::fromLatin1(pair.name));
        QVERIFY2(serverValue != INT_MIN,
                 qPrintable(QString("服务端 constants.h 里找不到 %1").arg(pair.name)));
        QVERIFY2(serverValue == pair.clientValue,
                 qPrintable(QString("错误码 %1 不一致：客户端=%2，服务端=%3")
                                .arg(pair.name)
                                .arg(pair.clientValue)
                                .arg(serverValue)));
    }
}

void TestProtocolContract::clientLocalCodesAvoidServerRange()
{
    // 客户端本地码（网络失败/解析失败）只在本进程内产生，不能与服务端错误码同值，
    // 否则会把「服务端某个业务失败」显示成本地错误——历史上 ERR_NETWORK 曾为 5，
    // 与 EMAIL_NOT_MATCH 撞值，「姓名与邮箱不匹配」被提示成「网络异常」。
    const int localCodes[] = {static_cast<int>(ErrorCodes::ERR_NETWORK),
                              static_cast<int>(ErrorCodes::JSON_PARSE_ERROR)};
    const QSet<int> serverCodes = {
        static_cast<int>(ErrorCodes::SUCCESS),
        static_cast<int>(ErrorCodes::ERROR_JSON),
        static_cast<int>(ErrorCodes::RPC_FAILED),
        static_cast<int>(ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED),
        static_cast<int>(ErrorCodes::USER_EXIST),
        static_cast<int>(ErrorCodes::EMAIL_NOT_MATCH),
        static_cast<int>(ErrorCodes::UPDATE_PASSWORD_FAILED),
        static_cast<int>(ErrorCodes::PASSWORD_NOT_MATCH),
        static_cast<int>(ErrorCodes::UID_INVALID),
        static_cast<int>(ErrorCodes::TOKEN_INVALID),
        static_cast<int>(ErrorCodes::USER_ALREADY_LOGIN),
        static_cast<int>(ErrorCodes::VERIFY_CODE_EXISTS),
        static_cast<int>(ErrorCodes::VERIFY_REDIS_ERROR),
        static_cast<int>(ErrorCodes::VERIFY_MAIL_FAILED),
    };

    for (const int code : localCodes) {
        QVERIFY2(code >= 5000, qPrintable(QString("本地码 %1 未落在 5000+ 预留段").arg(code)));
        QVERIFY2(!serverCodes.contains(code),
                 qPrintable(QString("本地码 %1 与服务端错误码撞值").arg(code)));
    }
    QVERIFY(static_cast<int>(ErrorCodes::ERR_NETWORK) != static_cast<int>(ErrorCodes::EMAIL_NOT_MATCH));
}

void TestProtocolContract::acceptActionsMatchServer()
{
    QString server;
    QVERIFY2(serverSourceReady(server), "读不到服务端 constants.h（检查 CHAT_REPO_ROOT）");

    QCOMPARE(testhelp::symbolValue(server, "FRIEND_ACTION_ACCEPT"), AcceptAction::ACCEPT);
    QCOMPARE(testhelp::symbolValue(server, "FRIEND_ACTION_REJECT"), AcceptAction::REJECT);
    QCOMPARE(AcceptAction::NONE, 0);
    QCOMPARE(testhelp::symbolValue(server, "APPLY_STATUS_PENDING"), 0);
    QCOMPARE(testhelp::symbolValue(server, "APPLY_STATUS_ACCEPTED"), 1);
    QCOMPARE(testhelp::symbolValue(server, "APPLY_STATUS_REJECTED"), 2);
}

void TestProtocolContract::httpPathsMatchGateServerRoutes()
{
    const QString gateSource = testhelp::readRepoFile("Servers/GateServer/src/logic_system.cpp");
    QVERIFY2(!gateSource.isEmpty(), "读不到 GateServer/src/logic_system.cpp（检查 CHAT_REPO_ROOT）");

    // 服务端注册的 POST 路由
    const QRegularExpression re(QStringLiteral("RegisterPost\\(\"([^\"]+)\""));
    QSet<QString> serverPaths;
    auto it = re.globalMatch(gateSource);
    while (it.hasNext()) {
        serverPaths.insert(it.next().captured(1));
    }

    const QStringList clientPaths = {HttpPaths::GET_VERIFY_CODE, HttpPaths::REGISTER_USER, HttpPaths::RESET_PASSWORD,
                                     HttpPaths::USER_LOGIN};
    for (const QString &path : clientPaths) {
        QVERIFY2(serverPaths.contains(path),
                 qPrintable(QString("客户端请求路径 %1 在 GateServer 未注册（服务端已注册：%2）")
                                .arg(path, QStringList(serverPaths.values()).join(", "))));
    }
}

void TestProtocolContract::avatarCountMatchesHeadImages()
{
    QString server;
    QVERIFY2(serverSourceReady(server), "读不到服务端 constants.h（检查 CHAT_REPO_ROOT）");
    const int serverAvatarCount = testhelp::symbolValue(server, "AVATAR_COUNT");
    QVERIFY(serverAvatarCount > 0);

    // 服务端注册时随机分配 head_1..AVATAR_COUNT，客户端必须有一一对应的内置头像
    QDir imageDir(QString::fromUtf8(CHAT_CLIENT_ROOT) + "/images");
    for (int i = 1; i <= serverAvatarCount; ++i) {
        const QString name = QStringLiteral("head_%1.jpg").arg(i);
        QVERIFY2(imageDir.exists(name),
                 qPrintable(QString("images/%1 缺失：服务端会分配该头像，客户端无法解析").arg(name)));
    }
}

QTEST_GUILESS_MAIN(TestProtocolContract)
#include "test_protocol_contract.moc"
