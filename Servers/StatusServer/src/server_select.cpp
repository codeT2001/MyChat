#include "server_select.h"

#include <charconv>
#include <climits>

#include "chat/log.h"

namespace P1 {
namespace {
int32_t ParseLoginCount(const std::string& raw)
{
    constexpr int32_t kMissingCount = INT_MAX >> 1;
    int32_t value = 0;
    const char* begin = raw.data();
    const char* end = begin + raw.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end || value < 0) {
        // 计数由各 ChatServer 自己维护；单条脏值只影响该节点，
        // 不能让整个刷新线程失败并长期保留旧缓存。
        LOG_WARN("[ServerSelect] invalid login_count:%s, fallback:%d", raw.c_str(), kMissingCount);
        return kMissingCount;
    }
    return value;
}
} // namespace

ChatServer PickMinServer(const std::unordered_map<std::string, ChatServer>& snapshot,
    const std::unordered_map<std::string, std::string>& count_map)
{
    if (snapshot.empty()) {
        return ChatServer();
    }
    ChatServer minServer;
    minServer.con_count = INT_MAX;
    for (const auto& [name, server] : snapshot) {
        ChatServer candidate = server;
        auto it = count_map.find(candidate.name);
        candidate.con_count = (it != count_map.end())
            ? ParseLoginCount(it->second)
            : (INT_MAX >> 1);
        if (candidate < minServer) {
            minServer = candidate;
        }
    }
    return minServer;
}

} // namespace P1
