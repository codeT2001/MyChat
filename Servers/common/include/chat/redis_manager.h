#ifndef _P1_REDIS_MANAGER_H_
#define _P1_REDIS_MANAGER_H_

#include <memory>
#include <optional>
#include <sw/redis++/redis++.h>
#include <utility>
#include <vector>

#include "chat/noncopyable.h"
#include "chat/service_interfaces.h"
namespace P1 {

// Redis Stream 的一条消息。
struct RedisStreamEntry {
    std::string id;
    std::vector<std::pair<std::string, std::string>> fields;

    std::optional<std::string> Field(const std::string& name) const
    {
        for (const auto& [key, value] : fields) {
            if (key == name) {
                return value;
            }
        }
        return std::nullopt;
    }
};

// XPENDING 返回的待确认消息摘要。
struct RedisPendingEntry {
    std::string id;
    std::string consumer;
    int64_t idleMs = 0;
    int64_t deliveryCount = 0;
};

class RedisManagerPool : public IKeyValueStore {
public:
    static RedisManagerPool& GetInstance() { static RedisManagerPool instance; return instance; }
    DISALLOW_COPY_MOVE(RedisManagerPool);

    ~RedisManagerPool();
    bool Connect(const std::string& host, uint16_t port);
    /* ---------- string ---------- */
    void Set(const std::string& key, const std::string& val) override;
    // 带过期时间的写入：缓存类数据用，避免脏数据永久驻留
    void SetEx(const std::string& key, const std::string& val, std::chrono::seconds ttl) override;
    std::optional<std::string> Get(const std::string& key) override;
    KvCompareDeleteResult CompareAndDelete(
        const std::string& key, const std::string& expected) override;

    /* ---------- hash ---------- */
    void HSet(const std::string& key, const std::string& field, const std::string& val) override;
    std::optional<std::string> HGet(const std::string& key, const std::string& field) override;
    std::unordered_map<std::string, std::string> HGetAll(const std::string& key) override;
    // atomic increment by
    std::optional<std::string> HIncrBy(const std::string& key, const std::string& field, int64_t increment) override;

    /* ---------- set（节点注册表用） ---------- */
    int64_t SAdd(const std::string& key, const std::string& member);
    int64_t SRem(const std::string& key, const std::string& member);
    std::vector<std::string> SMembers(const std::string& key);

    /* ---------- stream（跨节点事件总线用） ---------- */
    // SET key val NX EX ttl：用于消费端幂等去重
    bool SetNxEx(const std::string& key, const std::string& val, std::chrono::seconds ttl);
    // XADD key [MAXLEN ~ maxLen] * field value ...；返回消息 ID，失败抛异常
    std::string XAdd(const std::string& key,
        const std::vector<std::pair<std::string, std::string>>& fields, int64_t maxLen = 0);
    // XGROUP CREATE key group id [MKSTREAM]；BUSYGROUP 视为成功
    bool XGroupCreate(const std::string& key, const std::string& group,
        const std::string& id = "$", bool mkstream = true);
    // XREADGROUP GROUP group consumer [COUNT n] [BLOCK ms] STREAMS key id
    std::vector<RedisStreamEntry> XReadGroup(const std::string& key, const std::string& group,
        const std::string& consumer, const std::string& id, int64_t count,
        std::chrono::milliseconds block);
    // XCLAIM key group consumer min-idle-time id ...
    std::vector<RedisStreamEntry> XClaim(const std::string& key, const std::string& group,
        const std::string& consumer, std::chrono::milliseconds minIdle,
        const std::vector<std::string>& ids);
    long long XAck(const std::string& key, const std::string& group, const std::string& id);
    // XPENDING key group start end count（详情形式：id/consumer/idle/deliveryCount）
    std::vector<RedisPendingEntry> XPending(const std::string& key, const std::string& group,
        const std::string& start, const std::string& end, int64_t count);
    long long XLen(const std::string& key);
    bool Expire(const std::string& key, std::chrono::seconds ttl);

    /* ---------- list ---------- */
    void LPush(const std::string& key, const std::string& val);
    void RPush(const std::string& key, const std::string& val);
    std::optional<std::string> LPop(const std::string& key);
    std::optional<std::string> RPop(const std::string& key);
    std::vector<std::string> LRange(const std::string& key, long start = 0, long stop = -1);

    /* ---------- ping ---------- */
    bool Ping() noexcept;

    /* ---------- generic ---------- */
    bool Del(const std::string& key) override;
    std::size_t Del(const std::vector<std::string>& keys);
    int64_t HDel(const std::string& key, const std::string& field);
    bool Exists(const std::string& key);

private:
    RedisManagerPool();
    // 返回底层连接，未连接时返回 nullptr（避免空指针解引用崩溃）
    sw::redis::Redis* Client() const;
    // 供 XREADGROUP BLOCK 等阻塞命令使用的独立连接，socket 超时更长。
    sw::redis::Redis* BlockingClient() const;
    std::unique_ptr<sw::redis::Redis> redis_;
    std::unique_ptr<sw::redis::Redis> blockingRedis_;
};
} // namespace P1

#endif // _P1_REDIS_MANAGER_H_
