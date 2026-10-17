#include "chat_event.h"

namespace P1 {

const char* ChatEventTypeName(ChatEventType type)
{
    switch (type) {
        case ChatEventType::AddFriend:
            return "add_friend";
        case ChatEventType::FriendAccepted:
            return "friend_accepted";
        case ChatEventType::TextChatMsg:
            return "text_chat_msg";
        case ChatEventType::KickUser:
            return "kick_user";
    }
    return "unknown";
}

std::optional<ChatEventType> ParseChatEventType(const std::string& name)
{
    if (name == "add_friend") {
        return ChatEventType::AddFriend;
    }
    if (name == "friend_accepted") {
        return ChatEventType::FriendAccepted;
    }
    if (name == "text_chat_msg") {
        return ChatEventType::TextChatMsg;
    }
    if (name == "kick_user") {
        return ChatEventType::KickUser;
    }
    return std::nullopt;
}

} // namespace P1
