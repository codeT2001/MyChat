#include "chat/node_info.h"

#include <jsoncpp/json/json.h>

namespace P1 {
namespace {
bool ReadRequiredString(const Json::Value& root, const char* key, std::string& value)
{
    if (!root.isMember(key) || !root[key].isString()) {
        return false;
    }
    value = root[key].asString();
    return !value.empty();
}
} // namespace

std::string ChatNodeKey(const std::string& name)
{
    return std::string(CHAT_NODE_KEY_PREFIX) + name;
}

std::string EncodeChatNodeInfo(const ChatNodeInfo& info)
{
    Json::Value root(Json::objectValue);
    root["name"] = info.name;
    root["rpcHost"] = info.rpcHost;
    root["rpcPort"] = info.rpcPort;
    root["publicHost"] = info.publicHost;
    root["tcpPort"] = info.tcpPort;
    root["updatedAtMs"] = static_cast<Json::Int64>(info.updatedAtMs);

    Json::FastWriter writer;
    std::string payload = writer.write(root);
    if (!payload.empty() && payload.back() == '\n') {
        payload.pop_back();
    }
    return payload;
}

bool DecodeChatNodeInfo(const std::string& payload, ChatNodeInfo& info)
{
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(payload, root) || !root.isObject()) {
        return false;
    }

    ChatNodeInfo parsed;
    if (!ReadRequiredString(root, "name", parsed.name) ||
        !ReadRequiredString(root, "rpcHost", parsed.rpcHost) ||
        !ReadRequiredString(root, "rpcPort", parsed.rpcPort) ||
        !ReadRequiredString(root, "tcpPort", parsed.tcpPort)) {
        return false;
    }
    // publicHost 可选：缺省时回落到 rpcHost，适用于只配置监听地址的部署。
    if (!ReadRequiredString(root, "publicHost", parsed.publicHost)) {
        parsed.publicHost = parsed.rpcHost;
    }
    if (root.isMember("updatedAtMs") && root["updatedAtMs"].isInt64()) {
        parsed.updatedAtMs = root["updatedAtMs"].asInt64();
    }
    info = std::move(parsed);
    return true;
}

std::string BuildRpcEndpoint(const ChatNodeInfo& info)
{
    if (info.rpcHost.empty() || info.rpcPort.empty()) {
        return {};
    }
    return info.rpcHost + ":" + info.rpcPort;
}

} // namespace P1
