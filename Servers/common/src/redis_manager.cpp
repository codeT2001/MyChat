#include "chat/redis_manager.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <hiredis/hiredis.h>
#include <mutex>
#include <queue>
#include <string>

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

std::string ReplyString(redisReply* reply)
{
    if (!reply || reply->type == REDIS_REPLY_NIL) {
        return {};
    }
    if (reply->type != REDIS_REPLY_STRING && reply->type != REDIS_REPLY_STATUS) {
        throw sw::redis::ProtoError("expect string reply");
    }
    return std::string(reply->str, static_cast<std::size_t>(reply->len));
}

int64_t ReplyInt(redisReply* reply)
{
    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        throw sw::redis::ProtoError("expect integer reply");
    }
    return reply->integer;
}

// 解析 [ [id, [field, value, ...]], ... ] 形式的流消息数组（XCLAIM 直接返回该结构）
std::vector<RedisStreamEntry> ParseEntryArray(redisReply* reply)
{
    std::vector<RedisStreamEntry> entries;
    if (!reply || reply->type == REDIS_REPLY_NIL) {
        return entries;
    }
    if (reply->type != REDIS_REPLY_ARRAY) {
        throw sw::redis::ProtoError("expect stream entry array");
    }
    for (std::size_t i = 0; i < reply->elements; ++i) {
        redisReply* item = reply->element[i];
        if (!item || item->type != REDIS_REPLY_ARRAY || item->elements < 2) {
            continue;
        }
        RedisStreamEntry entry;
        entry.id = ReplyString(item->element[0]);
        redisReply* fields = item->element[1];
        if (fields && fields->type == REDIS_REPLY_ARRAY) {
            for (std::size_t j = 0; j + 1 < fields->elements; j += 2) {
                entry.fields.emplace_back(ReplyString(fields->element[j]), ReplyString(fields->element[j + 1]));
            }
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

// 解析 XREADGROUP 的 [ [stream, [entry...]], ... ] 结构
std::vector<RedisStreamEntry> ParseXReadReply(redisReply* reply)
{
    std::vector<RedisStreamEntry> entries;
    if (!reply || reply->type == REDIS_REPLY_NIL) {
        return entries;
    }
    if (reply->type != REDIS_REPLY_ARRAY) {
        throw sw::redis::ProtoError("expect XREADGROUP array reply");
    }
    for (std::size_t i = 0; i < reply->elements; ++i) {
        redisReply* stream = reply->element[i];
        if (!stream || stream->type != REDIS_REPLY_ARRAY || stream->elements < 2) {
            continue;
        }
        auto parsed = ParseEntryArray(stream->element[1]);
        entries.insert(entries.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
    }
    return entries;
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

sw::redis::Redis* RedisManagerPool::BlockingClient() const
{
    return blockingRedis_ ? blockingRedis_.get() : redis_.get();
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

        // 阻塞命令（XREADGROUP BLOCK）不能复用 200ms socket 超时的普通连接，
        // 否则 Redis 未返回数据时会在 socket 层抛 EAGAIN。
        sw::redis::ConnectionOptions blockingOpts = opts;
        blockingOpts.socket_timeout = std::chrono::milliseconds(
            ConfigManager::GetInstance().GetValue<int64_t>("Redis", "BlockingSocketTimeoutMs", 5000));
        sw::redis::ConnectionPoolOptions blockingPoolOpts;
        blockingPoolOpts.size = 1;
        blockingPoolOpts.wait_timeout = std::chrono::milliseconds(100);
        blockingRedis_ = std::make_unique<sw::redis::Redis>(blockingOpts, blockingPoolOpts);

        if (redis_->ping() == "PONG" && blockingRedis_->ping() == "PONG") {
            LOG_INFO("[RedisManager] connect success, %s:%u", host.c_str(), port);
            return true;
        }
        LOG_ERROR("[RedisManager] ping failed, %s:%u", host.c_str(), port);
        redis_.reset();
        blockingRedis_.reset();
        return false;
    } catch (const sw::redis::Error& e) { // redis 专用异常
        LOG_ERROR("[RedisManager] connect error, %s:%u, error:%s", host.c_str(), port, e.what());
        redis_.reset(); // 确保对象为空
        blockingRedis_.reset();
        return false;
    } catch (const std::exception& e) { // 其他网络/系统异常
        LOG_ERROR("[RedisManager] connect exception, %s:%u, error:%s", host.c_str(), port, e.what());
        redis_.reset();
        blockingRedis_.reset();
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

KvCompareDeleteResult RedisManagerPool::CompareAndDelete(
    const std::string& key, const std::string& expected)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("CompareAndDelete", key);
        return KvCompareDeleteResult::Error;
    }

    // Lua 在 Redis 单线程内完成 GET + 比对 + DEL，避免并发下同一 token 被消费两次。
    // 返回：0=键不存在，1=值不匹配，2=已删除。
    static constexpr char script[] =
        "local value = redis.call('GET', KEYS[1]) "
        "if not value then return 0 end "
        "if value ~= ARGV[1] then return 1 end "
        "redis.call('DEL', KEYS[1]) "
        "return 2";

    try {
        std::vector<std::string> args { "EVAL", script, "1", key, expected };
        auto reply = client->command(args.begin(), args.end());
        if (!reply || reply->type != REDIS_REPLY_INTEGER) {
            LOG_ERROR("[RedisManager] CompareAndDelete unexpected reply, key:%s", key.c_str());
            return KvCompareDeleteResult::Error;
        }
        switch (reply->integer) {
        case 0:
            return KvCompareDeleteResult::NotFound;
        case 1:
            return KvCompareDeleteResult::Mismatch;
        case 2:
            return KvCompareDeleteResult::Deleted;
        default:
            LOG_ERROR("[RedisManager] CompareAndDelete unexpected result:%lld, key:%s",
                static_cast<long long>(reply->integer), key.c_str());
            return KvCompareDeleteResult::Error;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[RedisManager] CompareAndDelete failed, key:%s, error:%s", key.c_str(), e.what());
        return KvCompareDeleteResult::Error;
    }
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

/* ---------- set ---------- */
int64_t RedisManagerPool::SAdd(const std::string& key, const std::string& member)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("SAdd", key);
        return 0;
    }
    return client->sadd(key, member);
}

int64_t RedisManagerPool::SRem(const std::string& key, const std::string& member)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("SRem", key);
        return 0;
    }
    return client->srem(key, member);
}

std::vector<std::string> RedisManagerPool::SMembers(const std::string& key)
{
    std::vector<std::string> members;
    auto* client = Client();
    if (!client) {
        LogUnavailable("SMembers", key);
        return members;
    }
    client->smembers(key, std::back_inserter(members));
    return members;
}

/* ---------- stream ---------- */
bool RedisManagerPool::SetNxEx(const std::string& key, const std::string& val, std::chrono::seconds ttl)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("SetNxEx", key);
        return false;
    }
    return client->set(key, val, std::chrono::milliseconds(ttl), sw::redis::UpdateType::NOT_EXIST);
}

std::string RedisManagerPool::XAdd(const std::string& key,
    const std::vector<std::pair<std::string, std::string>>& fields, int64_t maxLen)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("XAdd", key);
        return {};
    }
    std::vector<std::string> args { "XADD", key };
    if (maxLen > 0) {
        args.emplace_back("MAXLEN");
        args.emplace_back("~");
        args.push_back(std::to_string(maxLen));
    }
    args.emplace_back("*");
    for (const auto& [field, value] : fields) {
        args.push_back(field);
        args.push_back(value);
    }
    auto reply = client->command(args.begin(), args.end());
    return ReplyString(reply.get());
}

bool RedisManagerPool::XGroupCreate(const std::string& key, const std::string& group,
    const std::string& id, bool mkstream)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("XGroupCreate", key);
        return false;
    }
    std::vector<std::string> args { "XGROUP", "CREATE", key, group, id };
    if (mkstream) {
        args.emplace_back("MKSTREAM");
    }
    try {
        client->command(args.begin(), args.end());
        return true;
    } catch (const sw::redis::Error& e) {
        // 重复创建是正常启动路径
        if (std::string(e.what()).find("BUSYGROUP") != std::string::npos) {
            return true;
        }
        throw;
    }
}

std::vector<RedisStreamEntry> RedisManagerPool::XReadGroup(const std::string& key,
    const std::string& group, const std::string& consumer, const std::string& id,
    int64_t count, std::chrono::milliseconds block)
{
    auto* client = BlockingClient();
    if (!client) {
        LogUnavailable("XReadGroup", key);
        return {};
    }
    std::vector<std::string> args { "XREADGROUP", "GROUP", group, consumer };
    if (count > 0) {
        args.emplace_back("COUNT");
        args.push_back(std::to_string(count));
    }
    if (block.count() > 0) {
        args.emplace_back("BLOCK");
        args.push_back(std::to_string(block.count()));
    }
    args.emplace_back("STREAMS");
    args.push_back(key);
    args.push_back(id);
    auto reply = client->command(args.begin(), args.end());
    return ParseXReadReply(reply.get());
}

std::vector<RedisStreamEntry> RedisManagerPool::XClaim(const std::string& key,
    const std::string& group, const std::string& consumer,
    std::chrono::milliseconds minIdle, const std::vector<std::string>& ids)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("XClaim", key);
        return {};
    }
    if (ids.empty()) {
        return {};
    }
    std::vector<std::string> args { "XCLAIM", key, group, consumer,
        std::to_string(minIdle.count()) };
    args.insert(args.end(), ids.begin(), ids.end());
    auto reply = client->command(args.begin(), args.end());
    return ParseEntryArray(reply.get());
}

long long RedisManagerPool::XAck(const std::string& key, const std::string& group, const std::string& id)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("XAck", key);
        return 0;
    }
    return client->xack(key, group, id);
}

std::vector<RedisPendingEntry> RedisManagerPool::XPending(const std::string& key,
    const std::string& group, const std::string& start, const std::string& end, int64_t count)
{
    std::vector<RedisPendingEntry> pending;
    auto* client = Client();
    if (!client) {
        LogUnavailable("XPending", key);
        return pending;
    }
    std::vector<std::string> args { "XPENDING", key, group, start, end, std::to_string(count) };
    auto reply = client->command(args.begin(), args.end());
    if (!reply || reply->type == REDIS_REPLY_NIL) {
        return pending;
    }
    if (reply->type != REDIS_REPLY_ARRAY) {
        throw sw::redis::ProtoError("expect XPENDING array reply");
    }
    for (std::size_t i = 0; i < reply->elements; ++i) {
        redisReply* item = reply->element[i];
        if (!item || item->type != REDIS_REPLY_ARRAY || item->elements < 4) {
            continue;
        }
        RedisPendingEntry entry;
        entry.id = ReplyString(item->element[0]);
        entry.consumer = ReplyString(item->element[1]);
        entry.idleMs = ReplyInt(item->element[2]);
        entry.deliveryCount = ReplyInt(item->element[3]);
        pending.push_back(std::move(entry));
    }
    return pending;
}

long long RedisManagerPool::XLen(const std::string& key)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("XLen", key);
        return 0;
    }
    return client->xlen(key);
}

bool RedisManagerPool::Expire(const std::string& key, std::chrono::seconds ttl)
{
    auto* client = Client();
    if (!client) {
        LogUnavailable("Expire", key);
        return false;
    }
    return client->expire(key, ttl);
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
