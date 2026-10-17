#include "status_service_impl.h"

#include <climits>
#include <string>

#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "chat/node_registry.h"
#include "chat/redis_manager.h"
#include "status_flow.h"
namespace P1 {

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
    reply->set_token(GenerateLoginToken());
    StoreLoginToken(request->uid(), reply->token(), RedisManagerPool::GetInstance());
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
        configServers_[server.name] = server;
    }
    // 启动初期先用配置兜底；后台刷新线程会优先用 Redis 注册表覆盖。
    servers_ = configServers_;
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
    // token 校验逻辑抽到 status_flow.cpp，便于单测
    const StatusLoginResult result = ProcessStatusLogin(
        request->uid(), request->token(), RedisManagerPool::GetInstance());
    reply->set_error(result.error);
    if (result.error == static_cast<int32_t>(ErrorCodes::SUCCESS)) {
        reply->set_uid(result.uid);
        reply->set_token(result.token);
    }
    return Status::OK;
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
    // Step 1: 优先从 Redis 节点注册表读取当前存活节点。
    // 注册表为空（未升级/Redis 异常）时回落到 config.ini 的静态节点表。
    std::unordered_map<std::string, ChatServer> snapshot;
    try {
        for (const auto& node : ListChatNodes()) {
            ChatServer server;
            server.name = node.name;
            server.host = node.publicHost.empty() ? node.rpcHost : node.publicHost;
            server.port = node.tcpPort;

            // 迁移期兼容：StatusServer 旧配置里的 Host 是“下发到客户端的公网地址”，
            // ChatServer 的 SelfServer.Host 可能只是监听地址。配置存在时优先保留旧地址。
            auto cfgIter = configServers_.find(node.name);
            if (cfgIter != configServers_.end() && !cfgIter->second.host.empty()) {
                server.host = cfgIter->second.host;
            }
            if (server.host.empty() || server.port.empty()) {
                continue;
            }
            snapshot[server.name] = server;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[StatusServer] read node registry failed:%s", e.what());
    }
    if (snapshot.empty()) {
        snapshot = configServers_;
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
        // 注册表和配置都没有可用节点，保留上一轮缓存，避免被空数据覆盖
        LOG_WARN("[StatusServer] no chat server available, keep previous cache");
        return;
    }
    const ChatServer minServer = PickMinServer(snapshot, count_map);
    
    // ✅ Step 4: 写锁替换整个缓存（写锁持有时间极短）
    {
        std::unique_lock<std::shared_mutex> lock(rwMtx_);
        servers_ = std::move(snapshot);
        minServer_ = minServer;
    }
}
} // namespace P1
