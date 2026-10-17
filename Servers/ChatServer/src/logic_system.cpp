#include "logic_system.h"

#include "chat_flow.h"

#include <string>
#include <vector>

#include "chat/constants.h"
#include "chat/log.h"
#include "jsoncpp/json/json.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
#include "chat/status_grpc_client.h"
#include "chat/verify_grpc_client.h"
#include "user_manager.h"
#include "chat/config_manager.h"
#include "chat_grpc_client.h"
namespace P1 {
namespace {
// 查目标用户所在节点；不在线返回空
std::optional<std::string> GetPeerServer(int32_t uid)
{
    return RedisManagerPool::GetInstance().Get(USER_SERVER_PREFIX + std::to_string(uid));
}

// 目标用户是否在本节点（AddFriend/AuthFriend/TextChatMsg 三处共用的路由判断）
bool IsPeerLocal(const std::optional<std::string> &peerServer)
{
    return peerServer.has_value() &&
        *peerServer == ConfigManager::GetInstance().GetValue<std::string>("SelfServer", "Name");
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
LogicSystem::LogicSystem() : stop_(false)
{
    // 绑定生产实现（当前仅搜索用户流程使用，后续逐步迁移到 ChatDependencies）
    deps_.user_store = &MysqlManager::GetInstance();
    deps_.kv_store = &RedisManagerPool::GetInstance();
    RegisterHandleCallback();
    worker_ = std::thread(&LogicSystem::DealMessage, this);
}
LogicSystem::~LogicSystem()
{
    stop_ = true;
    cond_.notify_one();
    worker_.join();
}
void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> node)
{
    {
        std::lock_guard<std::mutex> lock(msgMtx_);
        msgQue_.push(node);
        LOG_INFO("[LogicSystem] recv, msgId:%u, len:%u", node->msgNode_->GetMsgId(), node->msgNode_->GetCurLen());
    }
    cond_.notify_one();
}

void LogicSystem::DealMessage()
{
    while (true) {
        std::shared_ptr<LogicNode> node;
        {
            std::unique_lock<std::mutex> lock(msgMtx_);
            cond_.wait(lock, [this]() { return stop_ || !msgQue_.empty(); });
            if (stop_ && msgQue_.empty()) {
                break;
            }
            if (msgQue_.empty()) {
                continue;
            }
            node = msgQue_.front();
            msgQue_.pop();
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
    if (!node || !node->msgNode_) {
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
    // 校验 token + 拉资料 + 组装申请/好友列表（纯逻辑，见 chat_flow.cpp）
    const LoginResult login = ProcessLogin(ctx.Request(), deps_);
    ctx.Response() = login.response;
    if (!login.ok) {
        return;
    }

    const int32_t uid = login.uid;
    const std::string uid_str = std::to_string(uid);
    auto serverName = ConfigManager::GetInstance().GetValue<std::string>("SelfServer", "Name");
    // 顶号：先取旧路由与同节点旧会话（必须在覆盖路由键、重绑会话之前），
    // 否则取到的都是新登录自己的状态
    auto oldServer = RedisManagerPool::GetInstance().Get(USER_SERVER_PREFIX + uid_str);
    std::shared_ptr<CSession> oldLocalSession;
    if (oldServer.has_value() && *oldServer == serverName) {
        oldLocalSession = UserManager::GetInstance().GetSession(uid);
    }
    RedisManagerPool::GetInstance().HIncrBy(LOGIN_COUNT, serverName, 1);
    RedisManagerPool::GetInstance().Set(USER_SERVER_PREFIX + uid_str, serverName);
    session->SetUserUid(uid);
    UserManager::GetInstance().SetUserSession(uid, session);
    // 新会话绑定成功后再踢旧会话：旧会话断开清理时 RmvUserSession 校验
    // 绑定的已不是自己，不会误删本次新登录的 Redis 状态
    if (oldServer.has_value() && !oldServer->empty()) {
        KickOldSession(uid, *oldServer, oldLocalSession);
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
        return;
    }
    // 旧会话在其他节点：跨节点 gRPC 通知踢人。
    // 旧节点已下线（残留路由键）时 RPC 会失败，忽略即可——
    // 路由键已被本次登录覆盖，残留状态就此自愈
    KickUserReq req;
    req.set_uid(uid);
    auto rsp = ChatGrpcClient::GetInstance().KickUser(oldServer, req);
    if (rsp.error() != static_cast<int32_t>(ErrorCodes::SUCCESS)) {
        LOG_WARN("[LogicSystem] kick remote session failed (stale route?), uid:%d, old server:%s",
            uid, oldServer.c_str());
    } else {
        LOG_WARN("[LogicSystem] kicked remote session, uid:%d, old server:%s", uid, oldServer.c_str());
    }
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
    ctx.Response() = ProcessSearchUser(ctx.Request(), deps_);
}

// from_uid ----> to_uid  apply_info name
void LogicSystem::HandleAddFriend(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    LOG_DEBUG("[LogicSystem] HandleAddFriend, msgId:%u", msgId);
    HandlerContext ctx(session, MSG_IDS::ADD_FRIEND_RSP, msg);
    const Json::Value& src = ctx.Request();
    Json::Value& rtValue = ctx.Response();
    auto from_uid = src["from_uid"].asInt();
    auto to_uid = src["to_uid"].asInt();
    auto name = src["name"].asString();
    auto desc = src["desc"].asString();
    auto remark_name = src["remark_name"].asString();
    (void)remark_name;

    // 通知逻辑结束后统一给申请者回包
    rtValue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);

    MysqlManager::GetInstance().AddFriendApply(from_uid, to_uid);
    auto to_server = GetPeerServer(to_uid);
    if (!to_server.has_value()) {
        LOG_INFO("[LogicSystem] HandleAddFriend, to_uid:%d offline", to_uid);
        return;
    }
    if (IsPeerLocal(to_server)) {
        auto psession = UserManager::GetInstance().GetSession(to_uid);
        if (!psession) {
            LOG_WARN("[LogicSystem] HandleAddFriend, to_uid:%d session not found", to_uid);
            return;
        }
        Json::Value val;
        val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
        val["desc"] = desc;
        val["name"] = name;
        val["apply_uid"] = from_uid;
        psession->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_ADD_FRIEND_REQ));
        return;
    }
    // 通过rpc通知
    std::string info_key = USER_INFO_PREFIX + std::to_string(from_uid);
    auto apply_user_info = std::make_shared<UserInfo>();
    AddFriendReq add_req;
    add_req.set_apply_uid(from_uid);
    add_req.set_to_uid(to_uid);
    add_req.set_name(name);
    add_req.set_desc(desc);
    if (GetBaseUserInfo(info_key, from_uid, apply_user_info)) {
        add_req.set_icon(apply_user_info->icon);
        add_req.set_sex(apply_user_info->sex);
        add_req.set_nick(apply_user_info->nick);
    }
    LOG_INFO("[LogicSystem] HandleAddFriend rpc to server:%s, from:%d, to:%d", to_server.value().c_str(), from_uid, to_uid);
    ChatGrpcClient::GetInstance().NotifyAddFriend(to_server.value(), add_req);
}

// 服务器接受客户端发送过来的好友认证请求 self_uid = to_uid
// 请求体 action：1=同意 2=拒绝（缺省按同意处理，兼容旧客户端）
void LogicSystem::HandleAuthFriend(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    LOG_DEBUG("[LogicSystem] HandleAuthFriend, msgId:%u", msgId);
    HandlerContext ctx(session, MSG_IDS::AUTH_FRIEND_RSP, msg);
    const Json::Value& src = ctx.Request();
    Json::Value& rtValue = ctx.Response();
    auto peer_uid = src["from_uid"].asInt();   // 申请者
    auto self_uid = src["to_uid"].asInt();     // 处理者（本人）
    auto remark_name = src["remark_name"].asString();
    // 旧客户端不带 action，默认同意
    auto action = src.isMember("action") ? src["action"].asInt() : FRIEND_ACTION_ACCEPT;

    rtValue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);

    if (action != FRIEND_ACTION_ACCEPT && action != FRIEND_ACTION_REJECT) {
        LOG_WARN("[LogicSystem] HandleAuthFriend invalid action:%d, from:%d, to:%d", action, peer_uid, self_uid);
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return;
    }

    // 响应里需要带【申请人】的 id/资料，客户端据此在申请列表中定位并更新该条申请
    std::string peer_key = USER_INFO_PREFIX + std::to_string(peer_uid);
    auto userInfo = std::make_shared<UserInfo>();
    if (GetBaseUserInfo(peer_key, peer_uid, userInfo)) {
        rtValue["uid"] = peer_uid;
        rtValue["from_uid"] = peer_uid;   // 申请人
        rtValue["to_uid"] = self_uid;     // 处理者（本人）
        rtValue["name"] = userInfo->name;
        rtValue["nick"] = userInfo->nick;
        rtValue["icon"] = userInfo->icon;
        rtValue["sex"] = userInfo->sex;
    }
    else {
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }
    rtValue["action"] = action;

    // 1) 同步申请状态：同意置 1，拒绝置 2
    const int32_t applyStatus =
        (action == FRIEND_ACTION_ACCEPT) ? APPLY_STATUS_ACCEPTED : APPLY_STATUS_REJECTED;
    if (!MysqlManager::GetInstance().AuthFriendApply(peer_uid, self_uid, applyStatus)) {
        LOG_WARN("[LogicSystem] HandleAuthFriend update apply status failed, from:%d, to:%d, action:%d",
            peer_uid, self_uid, action);
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }

    // 2) 同意才建立好友关系；拒绝不建立
    if (action == FRIEND_ACTION_ACCEPT &&
        !MysqlManager::GetInstance().AddFriend(peer_uid, self_uid, remark_name)) {
        LOG_WARN("[LogicSystem] HandleAuthFriend AddFriend failed, from:%d, to:%d", peer_uid, self_uid);
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }
    LOG_INFO("[LogicSystem] HandleAuthFriend %s, from:%d, to:%d",
        action == FRIEND_ACTION_ACCEPT ? "accepted" : "rejected", peer_uid, self_uid);

    // 3) 通知申请者处理结果（在线才通知，离线时其重新登录会拉取最新申请状态）
    NotifyAuthResult(peer_uid, self_uid, action);
}

// 通知申请者好友申请处理结果：在线才通知，离线时其重新登录会拉取最新申请状态
void LogicSystem::NotifyAuthResult(int32_t peer_uid, int32_t self_uid, int32_t action)
{
    auto peer_server = GetPeerServer(peer_uid);
    if (!peer_server.has_value()) {
        LOG_INFO("[LogicSystem] NotifyAuthResult, peer_uid:%d offline", peer_uid);
        return;
    }

    if (IsPeerLocal(peer_server)) {
        auto psession = UserManager::GetInstance().GetSession(peer_uid);
        if (!psession) {
            LOG_WARN("[LogicSystem] NotifyAuthResult, peer_uid:%d session not found", peer_uid);
            return;
        }
        Json::Value val;
        val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
        val["action"] = action;
        // 统一语义：from_uid=申请人（即接收通知者自己），to_uid=处理者
        val["from_uid"] = peer_uid;
        val["to_uid"] = self_uid;
        // 同意才携带处理者资料（客户端据此建好友会话）；拒绝只需 id + action
        if (action == FRIEND_ACTION_ACCEPT) {
            std::string self_key = USER_INFO_PREFIX + std::to_string(self_uid);
            auto uInfo = std::make_shared<UserInfo>();
            if (GetBaseUserInfo(self_key, self_uid, uInfo)) {
                val["name"] = uInfo->name;
                val["desc"] = uInfo->desc;
                val["nick"] = uInfo->nick;
                val["icon"] = uInfo->icon;
                val["sex"] = uInfo->sex;
            }
            else {
                val["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
            }
        }
        psession->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_AUTH_FRIEND_REQ));
        return;
    }
    // 通过rpc通知对端服务器
    FriendAcceptedReq accept_req;
    accept_req.set_from_uid(peer_uid);
    accept_req.set_to_uid(self_uid);
    accept_req.set_action(action);
    LOG_INFO("[LogicSystem] NotifyAuthResult rpc to server:%s, from:%d, to:%d, action:%d",
        peer_server.value().c_str(), peer_uid, self_uid, action);
    ChatGrpcClient::GetInstance().NotifyFriendAccepted(peer_server.value(), accept_req);
}

// 处理文本聊天消息 self_uid ---> peer_uid
void LogicSystem::HandleTextChatMsg(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg) {
    LOG_INFO("[LogicSystem] HandleTextChatMsg, msgId:%d, msg:%s", msgId, msg.c_str());
    HandlerContext ctx(session, MSG_IDS::TEXT_CHAT_MSG_RSP, msg);
    const Json::Value& src = ctx.Request();
    Json::Value& rtvalue = ctx.Response();
    auto self_uid = src["from_uid"].asInt();   // 发送者
    auto peer_uid = src["to_uid"].asInt();     // 接收者
    const Json::Value array = src["text_array"];
    rtvalue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    rtvalue["text_array"] = array;
    rtvalue["from_uid"] = self_uid;
    rtvalue["to_uid"] = peer_uid;

    //查询redis 查找peer_uid对应的server ip
    auto peer_server = GetPeerServer(peer_uid);
    if (!peer_server.has_value()) {
        return;
    }

    //直接通知对方有文本聊天消息
    if (IsPeerLocal(peer_server)) {
        auto psession = UserManager::GetInstance().GetSession(peer_uid);
        if (psession) {  
            //在内存中则直接发送通知对方
            std::string return_str = rtvalue.toStyledString();
            psession->SendMessage(return_str, static_cast<uint16_t>(MSG_IDS::NOTIFY_TEXT_CHAT_MSG_REQ));
        }
        return;
    }


    TextChatMsgReq text_msg_req;
    text_msg_req.set_from_uid(self_uid);
    text_msg_req.set_to_uid(peer_uid);
    for (const auto& e : array) {
        auto content = e["content"].asString();
        auto msg_id = e["msg_id"].asString();
        auto *text_msg = text_msg_req.add_msgs();
        text_msg->set_msg_id(msg_id);
        text_msg->set_msg_content(content);
    }
    //发送通知 todo...
    ChatGrpcClient::GetInstance().NotifyTextChatMsg(peer_server.value(), text_msg_req, rtvalue);

}
} // namespace P1
