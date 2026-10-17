#include "chat_event_consumer.h"

#include <algorithm>
#include <exception>
#include <unistd.h>
#include <utility>
#include <vector>

#include "chat/config_manager.h"
#include "chat/log.h"
#include "chat_event.h"
#include "chat_event_bus.h"
#include "message.grpc.pb.h"

namespace P1 {
namespace {
int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string ConsumerName(const std::string& nodeName)
{
    return nodeName + ":" + std::to_string(::getpid());
}
} // namespace

ChatEventConsumer::ChatEventConsumer(std::string nodeName)
    : nodeName_(std::move(nodeName)),
      streamKey_(ChatEventBus::StreamKey(nodeName_)),
      group_(nodeName_),
      consumer_(ConsumerName(nodeName_))
{
    auto& cfg = ConfigManager::GetInstance();
    batchSize_ = std::max<int64_t>(cfg.GetValue<int64_t>("Cluster", "StreamBatchSize", 64), 1);
    blockTime_ = std::chrono::milliseconds(
        std::max<int64_t>(cfg.GetValue<int64_t>("Cluster", "StreamBlockMs", 1000), 100));
    reclaimInterval_ = std::chrono::seconds(
        std::max<int64_t>(cfg.GetValue<int64_t>("Cluster", "StreamReclaimIntervalSeconds", 30), 1));
    pendingMinIdle_ = std::chrono::milliseconds(
        std::max<int64_t>(cfg.GetValue<int64_t>("Cluster", "StreamPendingMinIdleSeconds", 60), 1) * 1000);
    maxDeliveryAttempts_ = std::max<int64_t>(
        cfg.GetValue<int64_t>("Cluster", "StreamMaxDeliveryAttempts", 5), 1);
    streamTtl_ = std::chrono::seconds(
        std::max<int64_t>(cfg.GetValue<int64_t>("Cluster", "StreamKeyTtlSeconds", 86400), 60));
    dedupTtl_ = std::chrono::seconds(
        std::max<int64_t>(cfg.GetValue<int64_t>("Cluster", "DedupTtlSeconds", 600), 60));
    maxEventAge_ = std::chrono::seconds(
        cfg.GetValue<int64_t>("Cluster", "StreamMaxEventAgeSeconds", 300));
}

ChatEventConsumer::~ChatEventConsumer()
{
    Stop();
}

void ChatEventConsumer::Start()
{
    if (running_.exchange(true)) {
        return;
    }
    groupReady_ = EnsureGroup();
    lastReclaim_ = std::chrono::steady_clock::now();
    thread_ = std::thread(&ChatEventConsumer::Run, this);
}

void ChatEventConsumer::Stop()
{
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    LOG_INFO("[ChatEventConsumer] stopped, stream:%s", streamKey_.c_str());
}

bool ChatEventConsumer::EnsureGroup()
{
    try {
        auto& redis = RedisManagerPool::GetInstance();
        if (!redis.XGroupCreate(streamKey_, group_, "$", true)) {
            return false;
        }
        redis.Expire(streamKey_, streamTtl_);
        LOG_INFO("[ChatEventConsumer] group ready, stream:%s, group:%s, consumer:%s",
            streamKey_.c_str(), group_.c_str(), consumer_.c_str());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("[ChatEventConsumer] ensure group failed, stream:%s, error:%s",
            streamKey_.c_str(), e.what());
        return false;
    }
}

void ChatEventConsumer::Run()
{
    while (running_.load()) {
        if (!groupReady_) {
            groupReady_ = EnsureGroup();
            if (!groupReady_) {
                SleepWhileRunning(std::chrono::milliseconds(1000));
                continue;
            }
        }

        try {
            ConsumeNew();
        } catch (const std::exception& e) {
            LOG_ERROR("[ChatEventConsumer] consume failed, stream:%s, error:%s",
                streamKey_.c_str(), e.what());
            groupReady_ = false;
            SleepWhileRunning(std::chrono::milliseconds(1000));
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastReclaim_ >= reclaimInterval_) {
            lastReclaim_ = now;
            try {
                ReclaimPending();
            } catch (const std::exception& e) {
                LOG_ERROR("[ChatEventConsumer] reclaim failed, stream:%s, error:%s",
                    streamKey_.c_str(), e.what());
            }
        }
    }
}

void ChatEventConsumer::ConsumeNew()
{
    auto entries = RedisManagerPool::GetInstance().XReadGroup(streamKey_, group_, consumer_,
        ">", batchSize_, blockTime_);
    for (const auto& entry : entries) {
        if (!running_.load()) {
            break;
        }
        HandleEntry(entry);
    }
}

void ChatEventConsumer::ReclaimPending()
{
    auto& redis = RedisManagerPool::GetInstance();
    auto pending = redis.XPending(streamKey_, group_, "-", "+", batchSize_);
    std::vector<std::string> ids;
    std::unordered_map<std::string, int64_t> deliveries;
    for (const auto& item : pending) {
        if (item.idleMs < pendingMinIdle_.count()) {
            continue;
        }
        ids.push_back(item.id);
        deliveries[item.id] = item.deliveryCount;
    }
    if (ids.empty()) {
        return;
    }

    auto claimed = redis.XClaim(streamKey_, group_, consumer_, pendingMinIdle_, ids);
    for (auto& entry : claimed) {
        int64_t prior = 0;
        auto it = deliveries.find(entry.id);
        if (it != deliveries.end()) {
            prior = it->second;
        }
        if (prior >= maxDeliveryAttempts_) {
            DeadLetter(entry, "max delivery attempts exceeded");
            Ack(entry.id);
            attempts_.erase(entry.id);
            continue;
        }
        // XPENDING 的 deliveryCount 包含本次投递；转成失败次数用于统一重试计数。
        auto& attempts = attempts_[entry.id];
        attempts = std::max(attempts, prior - 1);
        HandleEntry(entry);
    }
}

void ChatEventConsumer::HandleEntry(const RedisStreamEntry& entry)
{
    if (IsDuplicate(entry.id)) {
        Ack(entry.id);
        attempts_.erase(entry.id);
        return;
    }
    if (IsExpired(entry)) {
        LOG_WARN("[ChatEventConsumer] drop expired event, stream:%s, id:%s",
            streamKey_.c_str(), entry.id.c_str());
        Ack(entry.id);
        attempts_.erase(entry.id);
        return;
    }

    if (ProcessEntry(entry)) {
        MarkProcessed(entry.id);
        Ack(entry.id);
        attempts_.erase(entry.id);
        return;
    }

    int64_t& attempts = attempts_[entry.id];
    ++attempts;
    if (attempts >= maxDeliveryAttempts_) {
        DeadLetter(entry, "processing failed after max attempts");
        Ack(entry.id);
        attempts_.erase(entry.id);
        return;
    }
    LOG_WARN("[ChatEventConsumer] processing failed, keep pending, stream:%s, id:%s, attempts:%lld",
        streamKey_.c_str(), entry.id.c_str(), static_cast<long long>(attempts));
}

bool ChatEventConsumer::ProcessEntry(const RedisStreamEntry& entry)
{
    auto typeName = entry.Field("type");
    auto payload = entry.Field("payload");
    if (!typeName.has_value() || !payload.has_value()) {
        DeadLetter(entry, "missing type or payload");
        return true;
    }

    auto type = ParseChatEventType(*typeName);
    if (!type.has_value()) {
        DeadLetter(entry, "unknown event type:" + *typeName);
        return true;
    }

    try {
        switch (*type) {
            case ChatEventType::AddFriend: {
                message::AddFriendReq req;
                if (!req.ParseFromString(*payload)) {
                    DeadLetter(entry, "invalid AddFriendReq payload");
                    return true;
                }
                notifier_.NotifyAddFriend(req);
                return true;
            }
            case ChatEventType::FriendAccepted: {
                message::FriendAcceptedReq req;
                if (!req.ParseFromString(*payload)) {
                    DeadLetter(entry, "invalid FriendAcceptedReq payload");
                    return true;
                }
                notifier_.NotifyFriendAccepted(req);
                return true;
            }
            case ChatEventType::TextChatMsg: {
                message::TextChatMsgReq req;
                if (!req.ParseFromString(*payload)) {
                    DeadLetter(entry, "invalid TextChatMsgReq payload");
                    return true;
                }
                notifier_.NotifyTextChatMsg(req);
                return true;
            }
            case ChatEventType::KickUser: {
                message::KickUserReq req;
                if (!req.ParseFromString(*payload)) {
                    DeadLetter(entry, "invalid KickUserReq payload");
                    return true;
                }
                notifier_.NotifyKickUser(req);
                return true;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[ChatEventConsumer] process exception, stream:%s, id:%s, type:%s, error:%s",
            streamKey_.c_str(), entry.id.c_str(), typeName->c_str(), e.what());
        return false;
    }
    return true;
}

bool ChatEventConsumer::IsDuplicate(const std::string& streamId) const
{
    try {
        return RedisManagerPool::GetInstance().Exists(DedupKey(streamId));
    } catch (const std::exception& e) {
        LOG_WARN("[ChatEventConsumer] dedup check failed, id:%s, error:%s", streamId.c_str(), e.what());
        return false;
    }
}

void ChatEventConsumer::MarkProcessed(const std::string& streamId)
{
    try {
        RedisManagerPool::GetInstance().SetNxEx(DedupKey(streamId), "1", dedupTtl_);
    } catch (const std::exception& e) {
        LOG_WARN("[ChatEventConsumer] mark processed failed, id:%s, error:%s", streamId.c_str(), e.what());
    }
}

bool ChatEventConsumer::IsExpired(const RedisStreamEntry& entry) const
{
    if (maxEventAge_.count() <= 0) {
        return false;
    }
    auto created = entry.Field("created_ms");
    if (!created.has_value()) {
        return false;
    }
    try {
        const int64_t createdMs = std::stoll(*created);
        return NowMs() - createdMs > maxEventAge_.count() * 1000;
    } catch (...) {
        return false;
    }
}

void ChatEventConsumer::Ack(const std::string& streamId)
{
    try {
        RedisManagerPool::GetInstance().XAck(streamKey_, group_, streamId);
    } catch (const std::exception& e) {
        LOG_WARN("[ChatEventConsumer] XACK failed, stream:%s, id:%s, error:%s",
            streamKey_.c_str(), streamId.c_str(), e.what());
    }
}

void ChatEventConsumer::DeadLetter(const RedisStreamEntry& entry, const std::string& reason)
{
    try {
        auto type = entry.Field("type");
        auto payload = entry.Field("payload");
        std::vector<std::pair<std::string, std::string>> fields {
            { "source_entry", entry.id },
            { "type", type.value_or("") },
            { "payload", payload.value_or("") },
            { "reason", reason },
            { "source", entry.Field("source").value_or("") },
            { "failed_ms", std::to_string(NowMs()) },
        };
        auto& redis = RedisManagerPool::GetInstance();
        const std::string deadKey = ChatEventBus::DeadLetterKey(nodeName_);
        redis.XAdd(deadKey, fields, 10000);
        redis.Expire(deadKey, streamTtl_);
        LOG_ERROR("[ChatEventConsumer] dead letter, stream:%s, id:%s, reason:%s",
            streamKey_.c_str(), entry.id.c_str(), reason.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("[ChatEventConsumer] write dead letter failed, id:%s, error:%s",
            entry.id.c_str(), e.what());
    }
}

void ChatEventConsumer::SleepWhileRunning(std::chrono::milliseconds duration) const
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (running_.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::string ChatEventConsumer::DedupKey(const std::string& streamId) const
{
    return "chat:dedup:" + nodeName_ + ":" + streamId;
}

} // namespace P1
