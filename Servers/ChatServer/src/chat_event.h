#ifndef _P1_CHAT_EVENT_H_
#define _P1_CHAT_EVENT_H_

#include <optional>
#include <string>

namespace P1 {

// 跨节点事件总线的消息类型；payload 为对应 protobuf 消息的二进制序列化。
enum class ChatEventType {
    AddFriend = 1,
    FriendAccepted = 2,
    TextChatMsg = 3,
    KickUser = 4,
};

const char* ChatEventTypeName(ChatEventType type);
std::optional<ChatEventType> ParseChatEventType(const std::string& name);

} // namespace P1

#endif // _P1_CHAT_EVENT_H_
