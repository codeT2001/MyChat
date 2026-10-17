#ifndef _P1_REDIS_MANAGER_H_
#define _P1_REDIS_MANAGER_H_

#include <memory>
#include <sw/redis++/redis++.h>

#include "chat/noncopyable.h"
#include "chat/service_interfaces.h"
namespace P1 {
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

    /* ---------- hash ---------- */
    void HSet(const std::string& key, const std::string& field, const std::string& val) override;
    std::optional<std::string> HGet(const std::string& key, const std::string& field) override;
    std::unordered_map<std::string, std::string> HGetAll(const std::string& key) override;
    // atomic increment by
    std::optional<std::string> HIncrBy(const std::string& key, const std::string& field, int64_t increment) override;

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
    std::unique_ptr<sw::redis::Redis> redis_;
};
} // namespace P1

#endif // _P1_REDIS_MANAGER_H_