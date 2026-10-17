#include <QtTest>

#include "constants.h"
#include "utils.h"

// 工具层：登录/注册表单校验、服务器地址与环境变量、协议 icon → 本地资源路径、文件大小格式化。
// 均为纯函数（不建界面、不建连接），因此无需 QApplication。

class TestUtils : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void emailValidation();
    void passwordValidation();
    void userNameValidation();
    void serverUrlUsesLocalDefault();
    void serverUrlHonorsEnvironmentOverride();
    void serverUrlIgnoresInvalidPort();
    void resolveIconMapping();
    void formatFileSize();
};

void TestUtils::cleanup()
{
    // 用例之间不残留环境变量，避免相互影响（GetServerUrl 每次调用都会读环境变量）
    qunsetenv("CHAT_SERVER_HOST");
    qunsetenv("CHAT_SERVER_PORT");
}

void TestUtils::emailValidation()
{
    QVERIFY(Utils::IsEmailValid("alice@example.com"));
    QVERIFY(Utils::IsEmailValid("a.b_c1@sub.example.com.cn"));

    QVERIFY(!Utils::IsEmailValid(""));
    QVERIFY(!Utils::IsEmailValid("alice"));
    QVERIFY(!Utils::IsEmailValid("alice@"));
    QVERIFY(!Utils::IsEmailValid("@example.com"));
    QVERIFY(!Utils::IsEmailValid("alice@example")); // 必须有顶级域
}

void TestUtils::passwordValidation()
{
    QVERIFY(Utils::IsPasswordValid("abc123"));
    QVERIFY(Utils::IsPasswordValid("a!b#c$d%e^f&g*h"));

    QVERIFY(!Utils::IsPasswordValid(""));
    QVERIFY(!Utils::IsPasswordValid("abc 123")); // 空格不允许
    QVERIFY(!Utils::IsPasswordValid("密码123"));
    QVERIFY(!Utils::IsPasswordValid("abc-123"));
}

void TestUtils::userNameValidation()
{
    QVERIFY(Utils::IsUserNameValid("alice_01"));
    QVERIFY(!Utils::IsUserNameValid(""));
    QVERIFY(!Utils::IsUserNameValid("alice-01"));
    QVERIFY(!Utils::IsUserNameValid("中文名"));
    QVERIFY(!Utils::IsUserNameValid("alice 01"));
}

void TestUtils::serverUrlUsesLocalDefault()
{
    // 默认必须是本地回环地址：仓库里不应提交生产/公网地址（见 Client/AGENTS.md）
    QCOMPARE(Utils::GetServerUrl(HttpPaths::USER_LOGIN), QString("http://127.0.0.1:9090/userLogin"));
    QCOMPARE(Utils::GetServerUrl(HttpPaths::REGISTER_USER), QString("http://127.0.0.1:9090/registerUser"));
}

void TestUtils::serverUrlHonorsEnvironmentOverride()
{
    qputenv("CHAT_SERVER_HOST", "192.168.1.5");
    qputenv("CHAT_SERVER_PORT", "8080");
    QCOMPARE(Utils::GetServerUrl(HttpPaths::USER_LOGIN), QString("http://192.168.1.5:8080/userLogin"));

    // 只覆盖 host 时端口保持默认
    qunsetenv("CHAT_SERVER_PORT");
    QCOMPARE(Utils::GetServerUrl(HttpPaths::USER_LOGIN), QString("http://192.168.1.5:9090/userLogin"));
}

void TestUtils::serverUrlIgnoresInvalidPort()
{
    qputenv("CHAT_SERVER_PORT", "not-a-port");
    QCOMPARE(Utils::GetServerUrl(HttpPaths::USER_LOGIN), QString("http://127.0.0.1:9090/userLogin"));

    qputenv("CHAT_SERVER_PORT", "0");
    QCOMPARE(Utils::GetServerUrl(HttpPaths::USER_LOGIN), QString("http://127.0.0.1:9090/userLogin"));
}

void TestUtils::resolveIconMapping()
{
    // 协议约定 head_N（与服务端 AVATAR_COUNT = 5 对应）
    QCOMPARE(Utils::ResolveIcon("head_1"), QString(":/images/head_1.jpg"));
    QCOMPARE(Utils::ResolveIcon("head_5"), QString(":/images/head_5.jpg"));
    // 超出范围/非法值/空串/历史占位一律回退默认头像
    QCOMPARE(Utils::ResolveIcon("head_0"), QString(":/images/head_1.jpg"));
    QCOMPARE(Utils::ResolveIcon("head_6"), QString(":/images/head_1.jpg"));
    QCOMPARE(Utils::ResolveIcon("head_x"), QString(":/images/head_1.jpg"));
    QCOMPARE(Utils::ResolveIcon("icon"), QString(":/images/head_1.jpg"));
    QCOMPARE(Utils::ResolveIcon(""), QString(":/images/head_1.jpg"));
    // 已是本地 qrc 路径则原样透传（如「新的朋友」用 add_friend.png）
    QCOMPARE(Utils::ResolveIcon(":/images/add_friend.png"), QString(":/images/add_friend.png"));
}

void TestUtils::formatFileSize()
{
    QCOMPARE(Utils::FormatFileSize(0), QString("0.00 B"));
    QCOMPARE(Utils::FormatFileSize(1023), QString("1023.00 B"));
    QCOMPARE(Utils::FormatFileSize(1024), QString("1.00 KB"));
    QCOMPARE(Utils::FormatFileSize(1024 * 1024 - 1), QString("1024.00 KB"));
    // 回归：1024*1024 恰好进入 MB 段（历史缺陷把上限写成 1024*1224，1MB~1.2MB 之间会显示成 KB）
    QCOMPARE(Utils::FormatFileSize(1024 * 1024), QString("1.00 MB"));
    QCOMPARE(Utils::FormatFileSize(1024 * 1224), QString("1.20 MB"));
    QCOMPARE(Utils::FormatFileSize(3 * 1024 * 1024 / 2), QString("1.50 MB"));
    QCOMPARE(Utils::FormatFileSize(1024LL * 1024 * 1024), QString("1.00 GB"));
}

QTEST_GUILESS_MAIN(TestUtils)
#include "test_utils.moc"
