#include "chat_event_bus.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <utility>
#include <vector>

#include "chat/config_manager.h"
#include "chat/log.h"
#include "chat/redis_manager.h"

namespace P1 {
namespace {
int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string MakeEventId(const std::string& node)
{
    static std::atomic<uint64_t> sequence { 0 };
    return node + "-" + std::to_string(NowMs()) + "-" + std::to_string(sequence.fetch_add(1));
}
} // namespace

ChatEventBus& ChatEventBus::GetInstance()
{
    static ChatEventBus instance;
    return instance;
}

ChatEventBus::ChatEventBus()
{
    auto& cfg = ConfigManager::GetInstance();
    selfNode_ = cfg.GetValue<std::string>("SelfServer", "Name", "");
    maxLen_ = cfg.GetValue<int64_t>("Cluster", "StreamMaxLen", 100000);
    streamTtlSeconds_ = cfg.GetValue<int64_t>("Cluster", "StreamKeyTtlSeconds", 86400);
    if (maxLen_ <= 0) {
        maxLen_ = 100000;
    }
    if (streamTtlSeconds_ <= 0) {
        streamTtlSeconds_ = 86400;
    }
}

std::string ChatEventBus::StreamKey(const std::string& node)
{
    return "chat:push:" + node;
}

std::string ChatEventBus::DeadLetterKey(const std::string& node)
{
    return "chat:dead:" + node;
}

bool ChatEventBus::Publish(const std::string& targetNode, ChatEventType type,
    const google::protobuf::Message& message)
{
    if (targetNode.empty()) {
        LOG_ERROR("[ChatEventBus] publish skipped, empty target node, type:%s", ChatEventTypeName(type));
        return false;
    }
    std::string payload;
    if (!message.SerializeToString(&payload)) {
        LOG_ERROR("[ChatEventBus] serialize failed, target:%s, type:%s",
            targetNode.c_str(), ChatEventTypeName(type));
        return false;
    }

    const std::string eventId = MakeEventId(selfNode_);
    std::vector<std::pair<std::string, std::string>> fields {
        { "type", ChatEventTypeName(type) },
        { "payload", std::move(payload) },
        { "source", selfNode_ },
        { "event_id", eventId },
        { "created_ms", std::to_string(NowMs()) },
    };

    try {
        auto& redis = RedisManagerPool::GetInstance();
        const std::string streamKey = StreamKey(targetNode);
        const std::string streamId = redis.XAdd(streamKey, fields, maxLen_);
        if (streamId.empty()) {
            LOG_ERROR("[ChatEventBus] XADD failed, target:%s, type:%s, event:%s",
                targetNode.c_str(), ChatEventTypeName(type), eventId.c_str());
            return false;
        }
        redis.Expire(streamKey, std::chrono::seconds(streamTtlSeconds_));
        LOG_DEBUG("[ChatEventBus] published, target:%s, type:%s, streamId:%s, event:%s",
            targetNode.c_str(), ChatEventTypeName(type), streamId.c_str(), eventId.c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[ChatEventBus] publish exception, target:%s, type:%s, event:%s, error:%s",
            targetNode.c_str(), ChatEventTypeName(type), eventId.c_str(), e.what());
        return false;
    }
}

} // namespace P1
