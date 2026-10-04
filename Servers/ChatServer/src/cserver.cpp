#include "cserver.h"

#include <utility>

#include "chat/asio_io_context_pool.h"
#include "chat/config_manager.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "chat/redis_manager.h"
#include "csession.h"
#include "user_manager.h"
namespace P1 {
CServer::CServer(boost::asio::io_context& io, unsigned short port)
    : io_(io), acceptor_(io, tcp::endpoint(tcp::v4(), port)), curId_(1)
{
}

void CServer::HandleAccept(std::shared_ptr<CSession> session, const boost::system::error_code& ec)
{
    if (!ec) {
        session->InitSocketOptions();
        session->SetRemoveCallback([this](uint32_t id) { this->RemoveSession(id); });
        session->Start();
        std::lock_guard<std::mutex> lock(sessionMtx_);
        sessions_.insert(std::make_pair(session->GetSessionId(), session));
    } else {
        LOG_ERROR("[CServer] session accept failed, error:%s", ec.message().c_str());
    }
    StartAccept();
}

std::shared_ptr<CSession> CServer::GetSession(uint32_t sessionId)
{
    std::lock_guard<std::mutex> lock(sessionMtx_);

    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return nullptr;
    }

    auto session = it->second.lock();
    if (!session) {
        sessions_.erase(it);
        return nullptr;
    }
    return session;
}

void CServer::RemoveSession(uint32_t sessionId)
{
    std::shared_ptr<CSession> session;
    {
        std::lock_guard<std::mutex> lock(sessionMtx_);
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            return;
        }
        session = it->second.lock();
        sessions_.erase(it);
    }
    if (!session) {
        return;
    }
    auto uid = session->GetUserUid();
    // 未登录的连接没有 Redis 状态（userId_ 默认为 0）
    if (uid == 0) {
        return;
    }
    // 只有当前生效的会话断开才清理 Redis；被新登录顶掉的旧会话不能清
    if (!UserManager::GetInstance().RmvUserSession(uid, session)) {
        return;
    }
    auto& redis = RedisManagerPool::GetInstance();
    auto uidStr = std::to_string(uid);
    auto serverName = ConfigManager::GetInstance().GetValue<std::string>("SelfServer", "Name");
    redis.HIncrBy(LOGIN_COUNT, serverName, -1);
    // 路由键可能已指向用户重新登录的其他节点，仅当仍指向本节点时才删
    auto routeKey = USER_SERVER_PREFIX + uidStr;
    auto route = redis.Get(routeKey);
    if (route.has_value() && *route == serverName) {
        redis.Del(routeKey);
    }
    // token 只服务于本次 TCP 登录，退出即失效，下次登录走 Gate 重新领取
    redis.Del(USER_TOKEN_PREFIX + uidStr);
    // 用户资料缓存一并清除，下次登录强制回源 MySQL，保证资料最新
    redis.Del(USER_INFO_PREFIX + uidStr);
    LOG_INFO("[CServer] session closed, redis cleaned, uid:%u, sessionId:%u", uid, sessionId);
}

void CServer::StartAccept()
{
    auto self = shared_from_this();
    uint32_t curId = ++curId_;
    auto session = std::make_shared<CSession>(AsioIOContextPool::GetInstance().GetIOService(), curId);
    acceptor_.async_accept(session->GetSocket(),
        [self, session](const boost::system::error_code& ec) { self->HandleAccept(session, ec); });
}

} // namespace P1
