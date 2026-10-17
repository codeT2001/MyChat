#include "server_select.h"

#include <climits>

namespace P1 {

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
            ? std::stoi(it->second)
            : (INT_MAX >> 1);
        if (candidate < minServer) {
            minServer = candidate;
        }
    }
    return minServer;
}

} // namespace P1
