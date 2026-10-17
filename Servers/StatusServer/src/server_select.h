#ifndef _P1_SERVER_SELECT_H_
#define _P1_SERVER_SELECT_H_

#include <cstdint>
#include <string>
#include <unordered_map>

namespace P1 {

// 一个 ChatServer 节点的路由信息（负载 = 在线连接数）
class ChatServer {
public:
    ChatServer() : host(""), port(""), name(""), con_count(0) {}
    ChatServer(const ChatServer& cs) : host(cs.host), port(cs.port), name(cs.name), con_count(cs.con_count) {}
    ChatServer& operator=(const ChatServer& cs)
    {
        if (&cs == this) {
            return *this;
        }
        host = cs.host;
        name = cs.name;
        port = cs.port;
        con_count = cs.con_count;
        return *this;
    }
    bool operator<(const ChatServer& cs) const
    {
        return con_count < cs.con_count;
    }
    std::string host;
    std::string port;
    std::string name;
    int32_t con_count;
};

// 从「节点快照 + 在线计数」中选出负载最小的节点（纯函数，便于单测）。
// count_map 中缺某节点时其负载按 INT_MAX>>1 计；snapshot 为空时返回默认构造的 ChatServer。
ChatServer PickMinServer(const std::unordered_map<std::string, ChatServer>& snapshot,
    const std::unordered_map<std::string, std::string>& count_map);

} // namespace P1

#endif // _P1_SERVER_SELECT_H_
