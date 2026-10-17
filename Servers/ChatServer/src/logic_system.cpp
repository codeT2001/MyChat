#include "logic_system.h"

#include "chat_flow.h"

#include <algorithm>
#include <string>
#include <vector>

#include "business_worker.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "jsoncpp/json/json.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
#include "chat/status_grpc_client.h"
#include "chat/verify_grpc_client.h"
#include "user_manager.h"
#include "chat/config_manager.h"
#include "chat_message_router.h"
namespace P1 {
using message::AddFriendReq;
using message::FriendAcceptedReq;
using message::KickUserReq;
using message::TextChatMsgReq;
namespace {
constexpr int32_t DEFAULT_BUSINESS_WORKERS = 4;
constexpr int32_t MAX_BUSINESS_WORKERS = 32;
constexpr int32_t DEFAULT_MAX_PENDING_PER_WORKER = 4096;

// 查目标用户所在节点；不在线返回空
std::optional<std::string> GetPeerServer(int32_t uid)
{
    return RedisManagerPool::GetInstance().Get(USER_SERVER_PREFIX + std::to_string(uid));
}

// 目标用户是否在本节点（AddFriend/AuthFriend/TextChatMsg 三处共用的路由判断）
bool IsPeerLocal(const std::optional<std::string> &peerServer, const std::string& selfServerName)
{
    return peerServer.has_value() &&
        *peerServer == selfServerName;
}

bool IsCurrentSession(int32_t uid, const std::shared_ptr<CSession>& session)
{
    return UserManager::GetInstance().GetSession(uid) == session;
}

// Handler 公共骨架：解析请求体，并在作用域结束时按 rspId 统一回包。
// 消除各 handler 里重复的「Json::Reader 解析 → 组装响应 JSON → Defer 回包」样板，
// 同时保证每条分支 return 都会回包（与原 Defer 行为一致）。
class HandlerContext {
public:
    HandlerContext(const std::shared_ptr<CSession> &session, MSG_IDS rspId, const std::string &msg)
        : session_(session), rspId_(rspId)
    {
        Json::Reader reader;
        reader.parse(msg, request_);
    }

    ~HandlerContext()
    {
        session_->SendMessage(response_.toStyledString(), static_cast<uint16_t>(rspId_));
    }

    const Json::Value &Request() const { return request_; }
    Json::Value &Response() { return response_; }

private:
    std::shared_ptr<CSession> session_;
    MSG_IDS rspId_;
    Json::Value request_;
    Json::Value response_;
};
} // namespace

struct LogicSystem::BusinessWorker {
    std::mutex mtx;
    std::condition_variable cv;
    std::queue<std::shared_ptr<LogicNode>> queue;
};

LogicSystem::LogicSystem() : stop_(false)
{
    // 绑定生产实现（当前仅搜索用户流程使用，后续逐步迁移到 ChatDependencies）
    deps_.user_store = &MysqlManager::GetInstance();
    deps_.kv_store = &RedisManagerPool::GetInstance();
    RegisterHandleCallback();

    auto& cfg = ConfigManager::GetInstance();
    selfServerName_ = cfg.GetValue<std::string>("SelfServer", "Name", "");
    int32_t workerCount = cfg.GetValue<int32_t>("Business", "WorkerThreads", DEFAULT_BUSINESS_WORKERS);
    workerCount = std::clamp(workerCount, 1, MAX_BUSINESS_WORKERS);
    const int32_t maxPending = cfg.GetValue<int32_t>(
        "Business", "MaxPendingPerWorker", DEFAULT_MAX_PENDING_PER_WORKER);
    maxPendingPerWorker_ = static_cast<std::size_t>(std::max(maxPending, 1));

    workers_.reserve(static_cast<std::size_t>(workerCount));
    workerThreads_.reserve(static_cast<std::size_t>(workerCount));
    for (int32_t i = 0; i < workerCount; ++i) {
        workers_.push_back(std::make_unique<BusinessWorker>());
    }
    for (std::size_t i = 0; i < workers_.size(); ++i) {
        workerThreads_.emplace_back(&LogicSystem::DealMessage, this, i);
    }
    LOG_INFO("[LogicSystem] business workers started, count:%d, maxPendingPerWorker:%zu",
        workerCount, maxPendingPerWorker_);
}
LogicSystem::~LogicSystem()
{
    stop_.store(true);
    for (auto& worker : workers_) {
        worker->cv.notify_all();
    }
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> node)
{
    if (!node || !node->session_ || !node->msgNode_ || stop_.load() || workers_.empty()) {
        return;
    }
    BusinessWorker& worker = *workers_[SelectBusinessWorker(
        node->session_->GetSessionId(), workers_.size())];
    bool overloaded = false;
    {
        std::lock_guard<std::mutex> lock(worker.mtx);
        if (worker.queue.size() >= maxPendingPerWorker_) {
            overloaded = true;
        } else {
            worker.queue.push(node);
        }
    }
    if (overloaded) {
        LOG_ERROR("[LogicSystem] business queue full, closing session, sessionId:%u, msgId:%u",
            node->session_->GetSessionId(), node->msgNode_->GetMsgId());
        node->session_->CloseAfterFlush();
        return;
    }
    // 日志放在锁外，避免慢日志放大队列锁竞争
    LOG_INFO("[LogicSystem] recv, msgId:%u, len:%u", node->msgNode_->GetMsgId(), node->msgNode_->GetCurLen());
    worker.cv.notify_one();
}

void LogicSystem::DealMessage(std::size_t workerIndex)
{
    BusinessWorker& worker = *workers_[workerIndex];
    while (true) {
        std::shared_ptr<LogicNode> node;
        {
            std::unique_lock<std::mutex> lock(worker.mtx);
            worker.cv.wait(lock, [this, &worker]() {
                return stop_.load() || !worker.queue.empty();
            });
            if (stop_.load() && worker.queue.empty()) {
                break;
            }
            if (worker.queue.empty()) {
                continue;
            }
            node = std::move(worker.queue.front());
            worker.queue.pop();
        }
        // 消息处理放在锁外：慢 handler（DB/RPC）不再阻塞新消息入队
        try {
            ProcessMessage(node);
        } catch (const std::exception& e) {
            // 单条消息处理异常不能拖垮整个业务线程，否则所有消息停止分发
            LOG_ERROR("[LogicSystem] process message exception, msgId:%u, error:%s",
                node->msgNode_ ? node->msgNode_->GetMsgId() : 0, e.what());
        } catch (...) {
            LOG_ERROR("[LogicSystem] process message unknown exception");
        }
    }
}

void LogicSystem::ProcessMessage(std::shared_ptr<LogicNode> node)
{
    if (!node || !node->msgNode_ || !node->session_ || node->session_->IsClosed()) {
        return;
    }
    auto iter = handleFuncs_.find(static_cast<MSG_IDS>(node->msgNode_->GetMsgId()));
    if (iter == handleFuncs_.end()) {
        LOG_WARN("[LogicSystem] no handler for msgId:%u", node->msgNode_->GetMsgId());
        return;
    }
    iter->second(node->session_, node->msgNode_->GetMsgId(),
        std::string(node->msgNode_->GetData(), node->msgNode_->GetCurLen()));
}

void LogicSystem::HandleLogin(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    // 注意：请求体含 token，禁止整体打印
    LOG_DEBUG("[LogicSystem] HandleLogin, msgId:%u", msgId);
    HandlerContext ctx(session, MSG_IDS::CHAT_LOGIN_RSP, msg);
    // 一条 TCP 连接只能绑定一个账号；切换账号必须重新建连，
    // 否则旧 uid 的会话映射会残留。
    if (session->GetUserUid() != 0) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::USER_ALREADY_LOGIN);
        LOG_WARN("[LogicSystem] login rejected, session already bound, sessionId:%u, uid:%u",
            session->GetSessionId(), session->GetUserUid());
        return;
    }

    // 校验 token + 拉资料 + 组装申请/好友列表（纯逻辑，见 chat_flow.cpp）
    const LoginResult login = ProcessLogin(ctx.Request(), deps_);
    ctx.Response() = login.response;
    if (!login.ok) {
        return;
    }
    if (session->IsClosed()) {
        LOG_WARN("[LogicSystem] login aborted, session closed during validation, sessionId:%u",
            session->GetSessionId());
        return;
    }

    const int32_t uid = login.uid;
    // 同一 uid 的登录绑定必须与断线清理串行；不同 uid 可并行。
    // ctx 在函数末尾析构回包，此时锁已释放（锁对象晚于 ctx 构造、先于 ctx 析构）。
    std::lock_guard<std::mutex> uidLock(UserManager::GetInstance().GetUidLock(uid));
    const std::string uid_str = std::to_string(uid);
    const std::string& serverName = selfServerName_;
    // 顶号：先取旧路由与同节点旧会话（必须在覆盖路由键、重绑会话之前）。
    auto oldServer = RedisManagerPool::GetInstance().Get(USER_SERVER_PREFIX + uid_str);
    auto oldLocalSession = UserManager::GetInstance().GetSession(uid);
    // 注意：即使旧会话已置 closed，只要映射仍在，就说明 RemoveSession 还没完成扣减；
    // 此时不能把它当成「本节点无活跃会话」，否则会多 +1 且旧会话清理时不会扣减。
    const bool hadActiveLocalSession = static_cast<bool>(oldLocalSession);

    if (!session->TrySetUserUid(static_cast<uint32_t>(uid))) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::USER_ALREADY_LOGIN);
        LOG_WARN("[LogicSystem] login binding race, sessionId:%u, uid:%d",
            session->GetSessionId(), uid);
        return;
    }

    // 同节点重复登录只是替换会话，在线连接数不变；只有本节点原本没有活跃会话时才 +1。
    // 这样旧会话断开时 RmvUserSession 返回 false，也不会因“少减一次”导致计数泄漏。
    if (!hadActiveLocalSession) {
        if (!RedisManagerPool::GetInstance().HIncrBy(LOGIN_COUNT, serverName, 1).has_value()) {
            LOG_WARN("[LogicSystem] login count increment failed, server:%s, uid:%d",
                serverName.c_str(), uid);
        }
    }
    RedisManagerPool::GetInstance().Set(USER_SERVER_PREFIX + uid_str, serverName);
    UserManager::GetInstance().SetUserSession(uid, session);

    // 新会话绑定成功后再踢旧会话：旧会话断开清理时 RmvUserSession 校验
    // 绑定的已不是自己，不会误删本次新登录的 Redis 状态。
    if (oldLocalSession) {
        KickOldSession(uid, serverName, oldLocalSession);
    }
    if (oldServer.has_value() && !oldServer->empty() && *oldServer != serverName) {
        KickOldSession(uid, *oldServer, nullptr);
    }
    LOG_INFO("[LogicSystem] login success, uid:%d, name:%s", uid, login.name.c_str());
}

void LogicSystem::KickOldSession(int32_t uid, const std::string& oldServer,
    const std::shared_ptr<CSession>& oldLocalSession)
{
    Json::Value val;
    val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    val["reason"] = "account logged in elsewhere";
    auto kickMsg = val.toStyledString();

    // 旧会话在本节点：直接发踢下线通知。
    // 不强制服务端关 socket——客户端收到通知后自行断开，
    // 走正常断开清理链路，避免异步写未刷出就被关闭
    if (oldLocalSession) {
        LOG_WARN("[LogicSystem] kick old session on self, uid:%d, sessionId:%u",
            uid, oldLocalSession->GetSessionId());
        oldLocalSession->SendMessage(kickMsg, static_cast<uint16_t>(MSG_IDS::NOTIFY_KICK));
        // 顶层客户端可能忽略通知；服务端必须主动关闭旧连接，避免两个会话同时有效。
        oldLocalSession->CloseAfterFlush();
        return;
    }
    // 旧会话在其他节点：走统一跨节点出口（Stream 或 gRPC），异步派发。
    // 旧节点已下线（残留路由键）时 RPC 会失败，忽略即可——
    // 路由键已被本次登录覆盖，残留状态就此自愈
    KickUserReq req;
    req.set_uid(uid);
    ChatMessageRouter::GetInstance().KickUser(oldServer, req);
}

void LogicSystem::RegisterHandleCallback()
{
    handleFuncs_[MSG_IDS::CHAT_LOGIN_REQ] = [this](std::shared_ptr<CSession> session, const uint16_t msgId,
                                            const std::string& msg) { this->HandleLogin(session, msgId, msg); };
    handleFuncs_[MSG_IDS::SEARCH_USER_REQ] = [this](std::shared_ptr<CSession> session, const uint16_t msgId,
                                            const std::string& msg) { this->HandleSearchUser(session, msgId, msg); };
    handleFuncs_[MSG_IDS::ADD_FRIEND_REQ] = [this](std::shared_ptr<CSession> session, const uint16_t msgId,
                                            const std::string& msg) { this->HandleAddFriend(session, msgId, msg); };
    handleFuncs_[MSG_IDS::AUTH_FRIEND_REQ] = [this](std::shared_ptr<CSession> session, const uint16_t msgId,
                                            const std::string& msg) { this->HandleAuthFriend(session, msgId, msg); };
    handleFuncs_[MSG_IDS::TEXT_CHAT_MSG_REQ] = [this](std::shared_ptr<CSession> session, const uint16_t msgId,
                                            const std::string& msg) { this->HandleTextChatMsg(session, msgId, msg); };

}

bool LogicSystem::GetBaseUserInfo(const std::string baseKey, int32_t uid, std::shared_ptr<UserInfo>& userInfo)
{
    // Redis 缓存优先，未命中回落 MySQL 并写回缓存（实现见 chat_flow.cpp）
    return LoadBaseUserInfo(baseKey, uid, userInfo, deps_);
}

// 支持uid查询
void LogicSystem::HandleSearchUser(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    LOG_DEBUG("[LogicSystem] HandleSearchUser, msgId:%u", msgId);
    HandlerContext ctx(session, MSG_IDS::SEARCH_USER_RSP, msg);
    const int32_t actorUid = static_cast<int32_t>(session->GetUserUid());
    if (actorUid <= 0) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }
    std::lock_guard<std::mutex> uidLock(UserManager::GetInstance().GetUidLock(actorUid));
    if (!IsCurrentSession(actorUid, session)) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::USER_ALREADY_LOGIN);
        LOG_WARN("[LogicSystem] SearchUser rejected, stale session, uid:%d, sessionId:%u",
            actorUid, session->GetSessionId());
        return;
    }
    ctx.Response() = ProcessSearchUser(ctx.Request(), deps_);
}

// from_uid ----> to_uid  apply_info name
void LogicSystem::HandleAddFriend(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    LOG_DEBUG("[LogicSystem] HandleAddFriend, msgId:%u", msgId);
    HandlerContext ctx(session, MSG_IDS::ADD_FRIEND_RSP, msg);
    const int32_t actorUid = static_cast<int32_t>(session->GetUserUid());
    if (actorUid <= 0) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }
    std::lock_guard<std::mutex> uidLock(UserManager::GetInstance().GetUidLock(actorUid));
    if (!IsCurrentSession(actorUid, session)) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::USER_ALREADY_LOGIN);
        LOG_WARN("[LogicSystem] AddFriend rejected, stale session, uid:%d, sessionId:%u",
            actorUid, session->GetSessionId());
        return;
    }
    // 落库 + 回包 + 本地通知内容（纯逻辑，见 chat_flow.cpp）
    const AddFriendPlan plan = ProcessAddFriend(ctx.Request(), actorUid, deps_);
    ctx.Response() = plan.response;

    auto to_server = GetPeerServer(plan.toUid);
    if (!to_server.has_value()) {
        LOG_INFO("[LogicSystem] HandleAddFriend, to_uid:%d offline", plan.toUid);
        return;
    }
    if (IsPeerLocal(to_server, selfServerName_)) {
        auto psession = UserManager::GetInstance().GetSession(plan.toUid);
        if (!psession) {
            LOG_WARN("[LogicSystem] HandleAddFriend, to_uid:%d session not found", plan.toUid);
            return;
        }
        psession->SendMessage(plan.notify.toStyledString(),
            static_cast<uint16_t>(MSG_IDS::NOTIFY_ADD_FRIEND_REQ));
        return;
    }
    // 通过rpc通知
    const std::string info_key = std::string(USER_INFO_PREFIX) + std::to_string(plan.fromUid);
    auto apply_user_info = std::make_shared<UserInfo>();
    AddFriendReq add_req;
    add_req.set_apply_uid(plan.fromUid);
    add_req.set_to_uid(plan.toUid);
    add_req.set_name(plan.notify["name"].asString());
    add_req.set_desc(plan.notify["desc"].asString());
    if (GetBaseUserInfo(info_key, plan.fromUid, apply_user_info)) {
        add_req.set_icon(apply_user_info->icon);
        add_req.set_sex(apply_user_info->sex);
        add_req.set_nick(apply_user_info->nick);
    }
    const std::string peerServer = to_server.value();
    LOG_INFO("[LogicSystem] HandleAddFriend queue rpc, server:%s, from:%d, to:%d",
        peerServer.c_str(), plan.fromUid, plan.toUid);
    ChatMessageRouter::GetInstance().NotifyAddFriend(peerServer, add_req);
}

// 服务器接受客户端发送过来的好友认证请求 self_uid = to_uid
// 请求体 action：1=同意 2=拒绝（缺省按同意处理，兼容旧客户端）
void LogicSystem::HandleAuthFriend(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    LOG_DEBUG("[LogicSystem] HandleAuthFriend, msgId:%u", msgId);
    HandlerContext ctx(session, MSG_IDS::AUTH_FRIEND_RSP, msg);
    const int32_t actorUid = static_cast<int32_t>(session->GetUserUid());
    if (actorUid <= 0) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }
    std::lock_guard<std::mutex> uidLock(UserManager::GetInstance().GetUidLock(actorUid));
    if (!IsCurrentSession(actorUid, session)) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::USER_ALREADY_LOGIN);
        LOG_WARN("[LogicSystem] AuthFriend rejected, stale session, uid:%d, sessionId:%u",
            actorUid, session->GetSessionId());
        return;
    }
    // 校验 action → 更新申请状态 →（同意时）建立好友关系（纯逻辑，见 chat_flow.cpp）
    const AuthFriendPlan plan = ProcessAuthFriend(ctx.Request(), actorUid, deps_);
    ctx.Response() = plan.response;
    if (!plan.ok) {
        return;
    }
    // 通知申请者处理结果（在线才通知，离线时其重新登录会拉取最新申请状态）
    NotifyAuthResult(plan.peerUid, plan.selfUid, plan.action, plan.notify);
}

// 通知申请者好友申请处理结果：在线才通知，离线时其重新登录会拉取最新申请状态
// notify 由 ProcessAuthFriend 组装好（含同意时的处理者资料）
void LogicSystem::NotifyAuthResult(int32_t peer_uid, int32_t self_uid, int32_t action,
    const Json::Value& notify)
{
    auto peer_server = GetPeerServer(peer_uid);
    if (!peer_server.has_value()) {
        LOG_INFO("[LogicSystem] NotifyAuthResult, peer_uid:%d offline", peer_uid);
        return;
    }

    if (IsPeerLocal(peer_server, selfServerName_)) {
        auto psession = UserManager::GetInstance().GetSession(peer_uid);
        if (!psession) {
            LOG_WARN("[LogicSystem] NotifyAuthResult, peer_uid:%d session not found", peer_uid);
            return;
        }
        psession->SendMessage(notify.toStyledString(),
            static_cast<uint16_t>(MSG_IDS::NOTIFY_AUTH_FRIEND_REQ));
        return;
    }
    // 通过rpc通知对端服务器
    FriendAcceptedReq accept_req;
    accept_req.set_from_uid(peer_uid);
    accept_req.set_to_uid(self_uid);
    accept_req.set_action(action);
    const std::string peerServer = peer_server.value();
    LOG_INFO("[LogicSystem] NotifyAuthResult queue rpc, server:%s, from:%d, to:%d, action:%d",
        peerServer.c_str(), peer_uid, self_uid, action);
    ChatMessageRouter::GetInstance().NotifyFriendAccepted(peerServer, accept_req);
}

// 处理文本聊天消息 self_uid ---> peer_uid
void LogicSystem::HandleTextChatMsg(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    LOG_INFO("[LogicSystem] HandleTextChatMsg, msgId:%d", msgId);
    HandlerContext ctx(session, MSG_IDS::TEXT_CHAT_MSG_RSP, msg);
    const int32_t actorUid = static_cast<int32_t>(session->GetUserUid());
    if (actorUid <= 0) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }
    std::lock_guard<std::mutex> uidLock(UserManager::GetInstance().GetUidLock(actorUid));
    if (!IsCurrentSession(actorUid, session)) {
        ctx.Response()["error"] = static_cast<int32_t>(ErrorCodes::USER_ALREADY_LOGIN);
        LOG_WARN("[LogicSystem] TextChatMsg rejected, stale session, uid:%d, sessionId:%u",
            actorUid, session->GetSessionId());
        return;
    }
    // 回包 + 跨节点需要的消息列表（纯逻辑，见 chat_flow.cpp）
    const TextChatPlan plan = ProcessTextChatMsg(ctx.Request(), actorUid);
    ctx.Response() = plan.response;

    // 查询redis 查找 peer_uid 对应的 server
    auto peer_server = GetPeerServer(plan.toUid);
    if (!peer_server.has_value()) {
        return;
    }

    // 对方在本节点：直接通知
    if (IsPeerLocal(peer_server, selfServerName_)) {
        auto psession = UserManager::GetInstance().GetSession(plan.toUid);
        if (psession) {
            psession->SendMessage(plan.response.toStyledString(),
                static_cast<uint16_t>(MSG_IDS::NOTIFY_TEXT_CHAT_MSG_REQ));
        }
        return;
    }

    TextChatMsgReq text_msg_req;
    text_msg_req.set_from_uid(plan.fromUid);
    text_msg_req.set_to_uid(plan.toUid);
    for (const auto& [msg_id, content] : plan.msgs) {
        auto* text_msg = text_msg_req.add_msgs();
        text_msg->set_msg_id(msg_id);
        text_msg->set_msg_content(content);
    }
    const std::string peerServer = peer_server.value();
    ChatMessageRouter::GetInstance().NotifyTextChatMsg(peerServer, text_msg_req);
}
} // namespace P1
