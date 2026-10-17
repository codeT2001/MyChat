#ifndef _P1_CHAT_NODE_INFO_H_
#define _P1_CHAT_NODE_INFO_H_

#include <cstdint>
#include <string>

namespace P1 {

// Redis 中的 ChatServer 节点注册表：
//   SET   chat:nodes                      -> 节点名集合（SADD/SREM 维护）
//   STRING chat:node:{name}               -> 节点信息 JSON（带 TTL，心跳续期）
constexpr char CHAT_NODE_SET_KEY[] = "chat:nodes";
constexpr char CHAT_NODE_KEY_PREFIX[] = "chat:node:";

constexpr int32_t DEFAULT_NODE_TTL_SECONDS = 30;
constexpr int32_t DEFAULT_NODE_HEARTBEAT_SECONDS = 10;

// 一个 ChatServer 节点的注册信息。
// rpcHost/rpcPort 供其他 ChatServer 建立 gRPC 连接；
// publicHost/tcpPort 由 StatusServer 下发给客户端。
struct ChatNodeInfo {
    std::string name;
    std::string rpcHost;
    std::string rpcPort;
    std::string publicHost;
    std::string tcpPort;
    int64_t updatedAtMs = 0;
};

std::string ChatNodeKey(const std::string& name);

// 编解码只依赖标准库 + jsoncpp，便于单测；不访问 Redis。
std::string EncodeChatNodeInfo(const ChatNodeInfo& info);
bool DecodeChatNodeInfo(const std::string& payload, ChatNodeInfo& info);

// 返回 host:port；字段缺失时返回空串。
std::string BuildRpcEndpoint(const ChatNodeInfo& info);

} // namespace P1

#endif // _P1_CHAT_NODE_INFO_H_
