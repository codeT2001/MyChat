#ifndef _P1_CHAT_EVENT_BUS_H_
#define _P1_CHAT_EVENT_BUS_H_

#include <google/protobuf/message.h>
#include <cstdint>
#include <string>

#include "chat/noncopyable.h"
#include "chat_event.h"

namespace P1 {

// Redis Stream 发布端：每个 ChatServer 一条 chat:push:{node} 流。
// 事件 payload 直接使用 protobuf 二进制，收发两端共享 proto，避免再维护一套 JSON schema。
class ChatEventBus {
public:
    static ChatEventBus& GetInstance();
    DISALLOW_COPY_MOVE(ChatEventBus);

    bool Publish(const std::string& targetNode, ChatEventType type,
        const google::protobuf::Message& message);

    static std::string StreamKey(const std::string& node);
    static std::string DeadLetterKey(const std::string& node);

private:
    ChatEventBus();

    std::string selfNode_;
    int64_t maxLen_ = 100000;
    int64_t streamTtlSeconds_ = 86400;
};

} // namespace P1

#endif // _P1_CHAT_EVENT_BUS_H_
