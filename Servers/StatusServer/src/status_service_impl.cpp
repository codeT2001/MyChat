#include "status_service_impl.h"

#include <climits>
#include <string>

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
    // 节点列表只来自 Redis 注册表；静态 [ChatServers] 兼容已移除。
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
    // Step 1: 从 Redis 节点注册表读取当前存活节点。
    std::unordered_map<std::string, ChatServer> snapshot;
    try {
        for (const auto& node : ListChatNodes()) {
            ChatServer server;
            server.name = node.name;
            server.host = node.publicHost.empty() ? node.rpcHost : node.publicHost;
            server.port = node.tcpPort;
            if (server.host.empty() || server.port.empty()) {
                continue;
            }
            snapshot[server.name] = server;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[StatusServer] read node registry failed:%s", e.what());
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
        // 注册表没有可用节点，保留上一轮缓存，避免被空数据覆盖
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
