#include "test_framework.h"

#include "chat/constants.h"

// 协议契约回归：消息 ID / 错误码数值一旦变更即为不兼容，
// 这些断言把"不能改"的常量钉死，避免误改。
namespace {
int AsInt(P1::MSG_IDS id) { return static_cast<int>(id); }
int AsInt(P1::ErrorCodes code) { return static_cast<int>(code); }
} // namespace

TESTFW_TEST(Protocol_MsgIdsStable)
{
    CHECK_EQ(AsInt(P1::MSG_IDS::CHAT_LOGIN_REQ), 10001);
    CHECK_EQ(AsInt(P1::MSG_IDS::CHAT_LOGIN_RSP), 10002);
    CHECK_EQ(AsInt(P1::MSG_IDS::SEARCH_USER_REQ), 10003);
    CHECK_EQ(AsInt(P1::MSG_IDS::ADD_FRIEND_REQ), 10005);
    CHECK_EQ(AsInt(P1::MSG_IDS::AUTH_FRIEND_REQ), 10009);
    CHECK_EQ(AsInt(P1::MSG_IDS::TEXT_CHAT_MSG_REQ), 10013);
    CHECK_EQ(AsInt(P1::MSG_IDS::NOTIFY_KICK), 10017);
    CHECK_EQ(AsInt(P1::MSG_IDS::CHAT_HEARTBEAT), 99999);
}

TESTFW_TEST(Protocol_ErrorCodesStable)
{
    CHECK_EQ(AsInt(P1::ErrorCodes::SUCCESS), 0);
    CHECK_EQ(AsInt(P1::ErrorCodes::ERROR_JSON), 1);
    CHECK_EQ(AsInt(P1::ErrorCodes::RPC_FAILED), 2);
    CHECK_EQ(AsInt(P1::ErrorCodes::UID_INVALID), 8);
    CHECK_EQ(AsInt(P1::ErrorCodes::TOKEN_INVALID), 9);
    CHECK_EQ(AsInt(P1::ErrorCodes::VERIFY_CODE_EXISTS), 20001);
    CHECK_EQ(AsInt(P1::ErrorCodes::VERIFY_MAIL_FAILED), 20003);
}

TESTFW_TEST(Protocol_BusinessConstants)
{
    CHECK_EQ(P1::AVATAR_COUNT, 5);
    CHECK_EQ(P1::APPLY_STATUS_PENDING, 0);
    CHECK_EQ(P1::APPLY_STATUS_ACCEPTED, 1);
    CHECK_EQ(P1::APPLY_STATUS_REJECTED, 2);
    CHECK_EQ(P1::FRIEND_ACTION_ACCEPT, 1);
    CHECK_EQ(P1::FRIEND_ACTION_REJECT, 2);
}
