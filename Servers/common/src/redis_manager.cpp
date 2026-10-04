#include "chat/redis_manager.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "chat/config_manager.h"
#include "chat/log.h"
namespace P1 {
namespace {
const std::string DEFAULT_REDIS_HOST = "127.0.0.1";
const uint16_t DEFAULT_REDIS_PORT = 6379;
const size_t DEFAULT_POOLSIZE = 5;

// 连接不可用时的降频告警，避免每条消息都刷错误日志
void LogUnavailable(const std::string& op, const std::string& key)
{
    static std::atomic<bool> warned{false};
    bool expected = false;
    if (warned.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        LOG_ERROR("[RedisManager] redis not connected, skip operation:%s, key:%s", op.c_str(), key.c_str());
    }
}
} // namespace

RedisManagerPool::RedisManagerPool()
{
    auto& cfg = ConfigManager::GetInstance();

    const std::string sectionName = "Redis";
    std::string host = cfg.GetValue<std::string>(sectionName, "Host", DEFAULT_REDIS_HOST);
    uint16_t port = cfg.GetValue<uint16_t>(sectionName, "Port", DEFAULT_REDIS_PORT);

    Connect(host, port);
}

RedisManagerPool::~RedisManagerPool() = default;

sw::redis::Redis* RedisManagerPool::Client() const
{
    return redis_.get();
}

bool RedisManagerPool::Connect(const std::string& host, uint16_t port)
{
    const std::string prefix = "tcp://";
    try {
        sw::redis::ConnectionOptions opts;
        opts.host = host;
        opts.port = port;
        opts.socket_timeout = std::chrono::milliseconds(200); // 可选
        opts.connect_timeout = std::chrono::milliseconds(500);

        sw::redis::ConnectionPoolOptions pool_opts;
        pool_opts.size = DEFAULT_POOLSIZE; // 池大小
        pool_opts.wait_timeout = std::chrono::milliseconds(100);
        redis_ = std::make_unique<sw::redis::Redis>(opts, pool_opts);
        if (redis_->ping() == "PONG") {
            LOG_INFO("[RedisManager] connect success, %s:%u", host.c_str(), port);
            return true;
        }
        LOG_ERROR("[RedisManager] ping failed, %s:%u", host.c_str(), port);
        redis_.reset();
        return false;
    } catch (const sw::redis::Error& e) { // redis 专用异常
        LOG_ERROR("[RedisManager] connect error, %s:%u, error:%s", host.c_str(), port, e.what());
        redis_.reset(); // 确保对象为空
        return false;
    } catch (const std::exception& e) { // 其他网络/系统异常
        LOG_ERROR("[RedisManager] connect exception, %s:%u, error:%s", host.c_str(), port, e.what());
        redis_.reset();
        return false;
    }
}

/* ---------- string ---------- */
void RedisManagerPool::Set(const std::string& key, const std::string& val)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("Set", key);
        return;
    }
    client->set(key, val);
}

void RedisManagerPool::SetEx(const std::string& key, const std::string& val, std::chrono::seconds ttl)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("SetEx", key);
        return;
    }
    client->set(key, val, ttl);
}

std::optional<std::string> RedisManagerPool::Get(const std::string& key)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("Get", key);
        return std::nullopt;
    }
    auto val = client->get(key);
    return val ? std::optional<std::string>(*val) : std::nullopt;
}

/* ---------- hash ---------- */
void RedisManagerPool::HSet(const std::string& key, const std::string& field, const std::string& val)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("HSet", key);
        return;
    }
    client->hset(key, field, val);
}

std::optional<std::string> RedisManagerPool::HGet(const std::string& key, const std::string& field)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("HGet", key);
        return std::nullopt;
    }
    auto val = client->hget(key, field);
    return val ? std::optional<std::string>(*val) : std::nullopt;
}

std::unordered_map<std::string, std::string> RedisManagerPool::HGetAll(const std::string& key)
{
    std::unordered_map<std::string, std::string> dst;
    auto* client = Client();
    if (!client) {
        LogUnavailable("HGetAll", key);
        return dst;
    }
    client->hgetall(key, std::inserter(dst, dst.end()));
    return dst;
}

// atomic increment by
std::optional<std::string> RedisManagerPool::HIncrBy(const std::string& key, const std::string& field, int64_t increment)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("HIncrBy", key);
        return std::nullopt;
    }
    auto val = client->hincrby(key, field, increment);
    return val ? std::optional<std::string>(std::to_string(val)) : std::nullopt;
}

/* ---------- list ---------- */
void RedisManagerPool::LPush(const std::string& key, const std::string& val)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("LPush", key);
        return;
    }
    client->lpush(key, val);
}

void RedisManagerPool::RPush(const std::string& key, const std::string& val)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("RPush", key);
        return;
    }
    client->rpush(key, val);
}

std::optional<std::string> RedisManagerPool::LPop(const std::string& key)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("LPop", key);
        return std::nullopt;
    }
    auto val = client->lpop(key);
    return val ? std::optional<std::string>(*val) : std::nullopt;
}
std::optional<std::string> RedisManagerPool::RPop(const std::string& key)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("RPop", key);
        return std::nullopt;
    }
    auto val = client->rpop(key);
    return val ? std::optional<std::string>(*val) : std::nullopt;
}

std::vector<std::string> RedisManagerPool::LRange(const std::string& key, long start, long stop)
{
    std::vector<std::string> vec;
    auto* client = Client();
    if (!client) {
        LogUnavailable("LRange", key);
        return vec;
    }
    client->lrange(key, start, stop, std::back_inserter(vec));
    return vec;
}

/* ---------- ping ---------- */
bool RedisManagerPool::Ping() noexcept
{
    auto* client = Client();
    if (!client) {
        return false;
    }
    try {
        return client->ping() == "PONG";
    } catch (...) {
        return false;
    }
}

/* ---------- generic ---------- */
bool RedisManagerPool::Del(const std::string& key)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("Del", key);
        return false;
    }
    return client->del(key) == 1;
}
std::size_t RedisManagerPool::Del(const std::vector<std::string>& keys)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("Del", keys.empty() ? "" : keys.front());
        return 0;
    }
    return client->del(keys.begin(), keys.end());
}
int64_t RedisManagerPool::HDel(const std::string& key, const std::string& field)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("HDel", key);
        return 0;
    }
    return client->hdel(key, field);
}
bool RedisManagerPool::Exists(const std::string& key)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("Exists", key);
        return false;
    }
    return client->exists(key) == 1;
}

} // namespace P1
