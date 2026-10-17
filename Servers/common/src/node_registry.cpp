#include "chat/node_registry.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

#include "chat/log.h"
#include "chat/redis_manager.h"

namespace P1 {
namespace {
int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void SleepWhileRunning(const std::atomic<bool>& running, std::chrono::seconds duration)
{
    // 小步睡眠，保证 Stop() 能快速返回。
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (running.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
} // namespace

std::optional<ChatNodeInfo> ResolveChatNode(const std::string& name)
{
    if (name.empty()) {
        return std::nullopt;
    }
    try {
        auto payload = RedisManagerPool::GetInstance().Get(ChatNodeKey(name));
        if (!payload.has_value()) {
            return std::nullopt;
        }
        ChatNodeInfo info;
        if (!DecodeChatNodeInfo(*payload, info)) {
            LOG_ERROR("[NodeRegistry] decode node failed, name:%s", name.c_str());
            return std::nullopt;
        }
        return info;
    } catch (const std::exception& e) {
        LOG_ERROR("[NodeRegistry] resolve node failed, name:%s, error:%s", name.c_str(), e.what());
        return std::nullopt;
    }
}

std::vector<ChatNodeInfo> ListChatNodes()
{
    std::vector<ChatNodeInfo> nodes;
    std::vector<std::string> names;
    try {
        names = RedisManagerPool::GetInstance().SMembers(CHAT_NODE_SET_KEY);
    } catch (const std::exception& e) {
        LOG_ERROR("[NodeRegistry] list nodes failed, error:%s", e.what());
        return nodes;
    }

    for (const auto& name : names) {
        std::optional<std::string> payload;
        try {
            payload = RedisManagerPool::GetInstance().Get(ChatNodeKey(name));
        } catch (const std::exception& e) {
            // 瞬时 Redis 错误不能当成“节点已过期”，否则会误删存活节点。
            LOG_WARN("[NodeRegistry] read node failed, keep membership, name:%s, error:%s",
                name.c_str(), e.what());
            continue;
        }
        if (!payload.has_value()) {
            // 注册键已过期：清掉 SET 中的陈旧成员，避免无限累积。
            try {
                RedisManagerPool::GetInstance().SRem(CHAT_NODE_SET_KEY, name);
            } catch (const std::exception& e) {
                LOG_WARN("[NodeRegistry] cleanup stale node failed, name:%s, error:%s",
                    name.c_str(), e.what());
            }
            continue;
        }
        ChatNodeInfo info;
        if (!DecodeChatNodeInfo(*payload, info)) {
            // 注册内容异常时只跳过，不删除成员，留给人工排查。
            LOG_ERROR("[NodeRegistry] decode node failed, name:%s", name.c_str());
            continue;
        }
        nodes.push_back(std::move(info));
    }
    return nodes;
}

ChatNodeRegistrar::ChatNodeRegistrar(ChatNodeInfo info, std::chrono::seconds ttl, std::chrono::seconds heartbeat)
    : info_(std::move(info)),
      ttl_(ttl.count() > 0 ? ttl : std::chrono::seconds(DEFAULT_NODE_TTL_SECONDS)),
      heartbeat_(heartbeat.count() > 0 ? heartbeat : std::chrono::seconds(DEFAULT_NODE_HEARTBEAT_SECONDS))
{
    if (info_.publicHost.empty()) {
        info_.publicHost = info_.rpcHost;
    }
    if (heartbeat_ >= ttl_) {
        heartbeat_ = std::max(std::chrono::seconds(1), ttl_ / 2);
        LOG_WARN("[NodeRegistry] heartbeat >= ttl, adjusted, name:%s, ttl:%llds, heartbeat:%llds",
            info_.name.c_str(), static_cast<long long>(ttl_.count()),
            static_cast<long long>(heartbeat_.count()));
    }
    running_.store(true);
    Register();
    heartbeatThread_ = std::thread(&ChatNodeRegistrar::HeartbeatLoop, this);
}

ChatNodeRegistrar::~ChatNodeRegistrar()
{
    Stop();
}

void ChatNodeRegistrar::Stop()
{
    if (!running_.exchange(false)) {
        if (heartbeatThread_.joinable()) {
            heartbeatThread_.join();
        }
        return;
    }
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    Unregister();
}

void ChatNodeRegistrar::Register()
{
    if (info_.name.empty() || info_.rpcHost.empty() || info_.rpcPort.empty() || info_.tcpPort.empty()) {
        LOG_ERROR("[NodeRegistry] register skipped, incomplete node info, name:%s", info_.name.c_str());
        return;
    }
    try {
        info_.updatedAtMs = NowMs();
        auto& redis = RedisManagerPool::GetInstance();
        redis.SetEx(ChatNodeKey(info_.name), EncodeChatNodeInfo(info_), ttl_);
        redis.SAdd(CHAT_NODE_SET_KEY, info_.name);
        registered_.store(true);
        LOG_DEBUG("[NodeRegistry] heartbeat, name:%s, ttl:%llds",
            info_.name.c_str(), static_cast<long long>(ttl_.count()));
    } catch (const std::exception& e) {
        registered_.store(false);
        LOG_ERROR("[NodeRegistry] register failed, name:%s, error:%s", info_.name.c_str(), e.what());
    }
}

void ChatNodeRegistrar::Unregister()
{
    try {
        auto& redis = RedisManagerPool::GetInstance();
        redis.SRem(CHAT_NODE_SET_KEY, info_.name);
        redis.Del(ChatNodeKey(info_.name));
        registered_.store(false);
        LOG_INFO("[NodeRegistry] unregistered, name:%s", info_.name.c_str());
    } catch (const std::exception& e) {
        LOG_WARN("[NodeRegistry] unregister failed, name:%s, error:%s", info_.name.c_str(), e.what());
    }
}

void ChatNodeRegistrar::HeartbeatLoop()
{
    while (running_.load()) {
        SleepWhileRunning(running_, heartbeat_);
        if (!running_.load()) {
            break;
        }
        Register();
    }
}

} // namespace P1
