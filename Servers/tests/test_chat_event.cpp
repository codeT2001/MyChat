#include "test_framework.h"

#include <string>

#include "chat_event.h"

using P1::ChatEventType;

TESTFW_TEST(ChatEventType_name_roundtrip)
{
    const ChatEventType types[] = {
        ChatEventType::AddFriend,
        ChatEventType::FriendAccepted,
        ChatEventType::TextChatMsg,
        ChatEventType::KickUser,
    };
    for (const auto type : types) {
        const auto parsed = P1::ParseChatEventType(P1::ChatEventTypeName(type));
        CHECK(parsed.has_value());
        CHECK_EQ(static_cast<int>(*parsed), static_cast<int>(type));
    }
}

TESTFW_TEST(ParseChatEventType_rejects_unknown)
{
    CHECK(!P1::ParseChatEventType("unknown").has_value());
    CHECK(!P1::ParseChatEventType("").has_value());
}
