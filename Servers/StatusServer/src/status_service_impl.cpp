#include "status_service_impl.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <climits>
#include <string>

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "chat/redis_manager.h"
namespace P1 {
static thread_local boost::uuids::random_generator gen;
std::string generate_unique_string()
{
    // 将UUID转换为字符串
    std::string unique_string = boost::uuids::to_string(gen());
    return unique_string;
}

Status StatusServiceImpl::GetChatServer(
    ServerContext* context, const GetChatServerReq* request, GetChatServerRsp* reply)
{
    const auto& server = GetChatServer();
    if (server.host.empty() || server.port.empty()) {
        LOG_ERROR("[StatusServer] GetChatServer failed, no available chat server, uid:%d", request->uid());
        reply->set_error(static_cast<int32_t>(ErrorCodes::RPC_FAILED));
        return Status::OK;
    }
    reply->set_host(server.host);
    reply->set_port(server.port);
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    reply->set_token(generate_unique_string());
    InsertToken(request->uid(), reply->token());
    return Status::OK;
}

StatusServiceImpl::StatusServiceImpl()
{
    auto& cfg = ConfigManager::GetInstance();
    static const std::string sectionName = "ChatServers";
    auto nameList = cfg.GetValue<std::string>(sectionName, "Name");
    std::vector<std::string> names;
    std::stringstream ss(nameList);
    std::string name;
    while (std::getline(ss, name, ',')) {
        names.push_back(name);
    }
    for (auto& secName : names) {
        if (cfg.GetValue<std::string>(secName, "Name").empty()) {
            continue;
        }

        ChatServer server;
        server.name = cfg.GetValue<std::string>(secName, "Name");
        server.host = cfg.GetValue<std::string>(secName, "Host");
        server.port = cfg.GetValue<std::string>(secName, "Port");
        servers_[server.name] = server;
    }
    if (servers_.size()) {
        minServer_ = servers_.begin()->second;
    }
    StartCacheRefresher(std::chrono::seconds(1));
}

StatusServiceImpl::~StatusServiceImpl()
{
    StopCacheRefresher();
}
ChatServer StatusServiceImpl::GetChatServer()
{
    std::shared_lock<std::shared_mutex> lock(rwMtx_);
    if (servers_.empty()) {
        return {};
    }
    LOG_INFO("[StatusServer] select chat server, name:%s, host:%s, port:%s, conns:%d",
        minServer_.name.c_str(), minServer_.host.c_str(), minServer_.port.c_str(), minServer_.con_count);
    return minServer_;
}

Status StatusServiceImpl::Login(ServerContext* context, const LoginReq* request, LoginRsp* reply)
{
    auto uid = request->uid();
    auto& token = request->token();

    std::string uid_str = std::to_string(uid);
    std::string token_key = USER_TOKEN_PREFIX + uid_str;
    auto token_value = RedisManagerPool::GetInstance().Get(token_key);
    if (!token_value.has_value()) {
        LOG_WARN("[StatusServer] Login failed, token not found, uid:%d", uid);
        reply->set_error(static_cast<int32_t>(ErrorCodes::UID_INVALID));
        return Status::OK;
    }
    if (token_value.value() != token) {
        LOG_WARN("[StatusServer] Login failed, token mismatch, uid:%d", uid);
        reply->set_error(static_cast<int32_t>(ErrorCodes::TOKEN_INVALID));
        return Status::OK;
    }
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    reply->set_uid(uid);
    reply->set_token(token);
    return Status::OK;
}

void StatusServiceImpl::InsertToken(int uid, std::string token)
{
    std::string uid_str = std::to_string(uid);
    std::string token_key = USER_TOKEN_PREFIX + uid_str;
    RedisManagerPool::GetInstance().Set(token_key, token); // 需要在用户退出清理token
}

void StatusServiceImpl::StartCacheRefresher(std::chrono::seconds interval)
{
    running_ = true;
    refreshThread_ = std::thread([this, interval]() {
        while (running_) {
            try {
                RefreshCache();
            } catch (const std::exception& e) {
                // 刷新失败不能中断线程，记录日志后继续
                LOG_ERROR("[StatusServer] cache refresh failed:%s", e.what());
            }
            
            // ✅ 使用小步睡眠代替整段 sleep，支持快速退出
            auto elapsed = std::chrono::milliseconds(0);
            while (running_ && elapsed < interval) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                elapsed += std::chrono::milliseconds(100);
            }
        }
    });
}

void StatusServiceImpl::StopCacheRefresher()
{
    running_ = false;
    if (refreshThread_.joinable()) {
        refreshThread_.join();
    }
}

void StatusServiceImpl::RefreshCache()
{
     // Step 1: 读取服务器列表快照
    std::unordered_map<std::string, ChatServer> snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(rwMtx_);
        for (const auto& [name, server] : servers_) {
            snapshot[name] = server;
        }
    }
    
    // Step 2: 一次性拉取所有计数（IO 在后台线程，不影响请求）
    std::unordered_map<std::string, std::string> count_map;
    try {
        count_map = RedisManagerPool::GetInstance().HGetAll(LOGIN_COUNT);
    } catch (...) {
        // 拉取失败时保留上一轮缓存，不做任何更新
        return;
    }
    if (snapshot.empty()) {
        // 配置中没有任何 ChatServer，保留上一轮缓存，避免被空数据覆盖
        LOG_WARN("[StatusServer] no chat server configured, keep previous cache");
        return;
    }
    ChatServer minServer;
    minServer.con_count = INT_MAX;
    for (auto& [name, server] : snapshot) {
        auto it = count_map.find(server.name);
        server.con_count = (it != count_map.end())
            ? std::stoi(it->second)
            : (INT_MAX >> 1);
        if(server < minServer) {
            minServer = server;
        }
    }
    
    // ✅ Step 4: 写锁替换整个缓存（写锁持有时间极短）
    {
        std::unique_lock<std::shared_mutex> lock(rwMtx_);
        servers_ = std::move(snapshot);
        minServer_ = minServer;
    }
    LOG_DEBUG("[StatusServer] cache refreshed, selected:%s, conns:%d",
        minServer.name.c_str(), minServer.con_count);
}
} // namespace P1