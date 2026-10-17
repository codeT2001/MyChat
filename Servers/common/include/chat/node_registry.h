#ifndef _P1_CHAT_NODE_REGISTRY_H_
#define _P1_CHAT_NODE_REGISTRY_H_

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#include "chat/node_info.h"
#include "chat/noncopyable.h"

namespace P1 {

// 读取单个节点；不存在、已过期或内容非法时返回 nullopt。
std::optional<ChatNodeInfo> ResolveChatNode(const std::string& name);

// 枚举当前存活节点；顺带清理 SET 中已失效的成员。
std::vector<ChatNodeInfo> ListChatNodes();

// RAII 注册器：构造时写入注册表并启动心跳线程，析构时注销。
class ChatNodeRegistrar {
public:
    ChatNodeRegistrar(ChatNodeInfo info,
        std::chrono::seconds ttl = std::chrono::seconds(DEFAULT_NODE_TTL_SECONDS),
        std::chrono::seconds heartbeat = std::chrono::seconds(DEFAULT_NODE_HEARTBEAT_SECONDS));
    ~ChatNodeRegistrar();
    DISALLOW_COPY_MOVE(ChatNodeRegistrar);

    void Stop();
    bool registered() const { return registered_.load(); }

private:
    void Register();
    void Unregister();
    void HeartbeatLoop();

    ChatNodeInfo info_;
    std::chrono::seconds ttl_;
    std::chrono::seconds heartbeat_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    std::thread heartbeatThread_;
};

} // namespace P1

#endif // _P1_CHAT_NODE_REGISTRY_H_
