#ifndef _P1_CHAT_EVENT_CONSUMER_H_
#define _P1_CHAT_EVENT_CONSUMER_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>

#include "chat/noncopyable.h"
#include "chat/redis_manager.h"
#include "chat_notifier.h"

namespace P1 {

// 每个 ChatServer 消费自己的 chat:push:{node} 流：
//   - consumer group 保证同一条消息只会被一个消费者处理；
//   - XACK 后删除 pending；处理失败保留 pending，由 XCLAIM 重试；
//   - 超过最大投递次数写入 chat:dead:{node} 死信流；
//   - SET NX 去重键避免 XCLAIM 重放导致重复推送。
class ChatEventConsumer {
public:
    explicit ChatEventConsumer(std::string nodeName);
    ~ChatEventConsumer();
    DISALLOW_COPY_MOVE(ChatEventConsumer);

    void Start();
    void Stop();

private:
    void Run();
    bool EnsureGroup();
    void ConsumeNew();
    void ReclaimPending();

    // 返回 false 表示可重试失败（保留 pending）；true 表示已处理/已丢弃，可以 ACK。
    bool ProcessEntry(const RedisStreamEntry& entry);
    void HandleEntry(const RedisStreamEntry& entry);
    bool IsDuplicate(const std::string& streamId) const;
    void MarkProcessed(const std::string& streamId);
    bool IsExpired(const RedisStreamEntry& entry) const;
    void Ack(const std::string& streamId);
    void DeadLetter(const RedisStreamEntry& entry, const std::string& reason);
    void SleepWhileRunning(std::chrono::milliseconds duration) const;
    std::string DedupKey(const std::string& streamId) const;

    std::string nodeName_;
    std::string streamKey_;
    std::string group_;
    std::string consumer_;
    ChatNotifier notifier_;

    std::atomic<bool> running_ { false };
    std::thread thread_;
    bool groupReady_ = false;

    int64_t batchSize_ = 64;
    std::chrono::milliseconds blockTime_ { 1000 };
    std::chrono::seconds reclaimInterval_ { 30 };
    std::chrono::milliseconds pendingMinIdle_ { 60000 };
    int64_t maxDeliveryAttempts_ = 5;
    std::chrono::seconds streamTtl_ { 86400 };
    std::chrono::seconds dedupTtl_ { 600 };
    std::chrono::seconds maxEventAge_ { 300 };
    std::chrono::steady_clock::time_point lastReclaim_ {};

    // 仅消费者线程访问；进程重启后以 XPENDING 的 deliveryCount 为准。
    std::unordered_map<std::string, int64_t> attempts_;
};

} // namespace P1

#endif // _P1_CHAT_EVENT_CONSUMER_H_
