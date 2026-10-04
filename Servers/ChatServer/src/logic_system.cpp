#include "logic_system.h"

#include <algorithm>
#include <cctype>

#include "chat/constants.h"
#include "chat/log.h"
#include "jsoncpp/json/json.h"
#include "jsoncpp/json/reader.h"
#include "jsoncpp/json/value.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
#include "chat/status_grpc_client.h"
#include "chat/verify_grpc_client.h"
#include "user_manager.h"
#include "chat/config_manager.h"
#include "chat_grpc_client.h"
namespace P1 {
LogicSystem::LogicSystem() : stop_(false)
{
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
    Json::Reader reader;
    Json::Value src;
    reader.parse(msg, src);
    auto uid = src["uid"].asInt();
    auto token = src["token"].asString();
    Json::Value retValue;
    // 无论从哪个分支返回，统一发送登录响应
    Defer defer([&]() {
        session->SendMessage(retValue.toStyledString(), static_cast<uint16_t>(MSG_IDS::CHAT_LOGIN_RSP));
    });

    // 查询token是否正确
    auto uid_str = std::to_string(uid);
    auto tokenKey = USER_TOKEN_PREFIX + uid_str;
    auto tokenValue = RedisManagerPool::GetInstance().Get(tokenKey);
    if (!tokenValue.has_value()) {
        retValue["error"] = (static_cast<int32_t>(ErrorCodes::UID_INVALID));
        LOG_ERROR("[LogicSystem] login failed, token not found in redis, uid:%d", uid);
        return;
    }
    if (tokenValue != token) {
        retValue["error"] = (static_cast<int32_t>(ErrorCodes::TOKEN_INVALID));
        LOG_ERROR("[LogicSystem] login failed, token invalid, uid:%d", uid);
        return;
    }
    auto baseKey = USER_INFO_PREFIX + uid_str;
    std::shared_ptr<UserInfo> userInfo = std::make_shared<UserInfo>();
    if (!GetBaseUserInfo(baseKey, uid, userInfo)) {
        retValue["error"] = static_cast<uint32_t>(ErrorCodes::UID_INVALID);
        LOG_ERROR("[LogicSystem] login failed, uid not found, uid:%d", uid);
        return;
    }
    retValue["error"] = static_cast<uint32_t>(ErrorCodes::SUCCESS);
    retValue["uid"] = uid;
    retValue["sex"] = userInfo->sex;
    retValue["name"] = userInfo->name;
    retValue["email"] = userInfo->email;
    retValue["pwd"] = userInfo->pwd;
    retValue["nick"] = userInfo->nick;
    retValue["desc"] = userInfo->desc;
    retValue["icon"] = userInfo->icon;
    retValue["token"] = token;
    // 获取申请列表
    std::vector<std::shared_ptr<ApplyInfo>> applyList;
    if (MysqlMganager::GetInstance().GetApplyList(uid, applyList, 0, 10)) {
        for (auto & apply : applyList) {
            Json::Value obj;
            obj["name"] = apply->name;
            obj["uid"] = apply->uid;
            obj["icon"] = apply->icon;
            obj["nick"] = apply->nick;
            obj["sex"] = apply->sex;
            obj["desc"] = apply->desc;
            obj["status"] = apply->status;
            retValue["apply_list"].append(obj);
        }
    }

    //获取好友列表
    std::vector<std::shared_ptr<UserInfo>> friendList;
    MysqlMganager::GetInstance().GetFriendList(uid, friendList);
    for (auto& f : friendList) {
        Json::Value obj;
        obj["name"] = f->name;
        obj["uid"] = f->uid;
        obj["icon"] = f->icon;
        obj["nick"] = f->nick;
        obj["sex"] = f->sex;
        obj["desc"] = f->desc;
        obj["back"] = f->back;
        retValue["friend_list"].append(obj);
    }

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
    LOG_INFO("[LogicSystem] login success, uid:%d, name:%s", uid, userInfo->name.c_str());
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
    // 先查Redis
    auto infoValue = RedisManagerPool::GetInstance().Get(baseKey);
    if (infoValue.has_value()) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(infoValue.value(), root);
        userInfo->uid = root["uid"].asInt();
        userInfo->sex = root["sex"].asInt();
        userInfo->name = root["name"].asString();
        userInfo->pwd = root["pwd"].asString();
        userInfo->email = root["email"].asString();
        userInfo->nick = root["nick"].asString();
        userInfo->desc = root["desc"].asString();
        userInfo->icon = root["icon"].asString();
        LOG_INFO("[LogicSystem] GetBaseUserInfo from redis, uid:%d, name:%s", uid, userInfo->name.c_str());
        return true;
    }
    auto info = MysqlMganager::GetInstance().GetUserByUid(uid);
    if (!info) {
        return false;
    }
    userInfo = info;
    Json::Value root;
    root["uid"] = uid;
    root["sex"] = info->sex;
    root["name"] = info->name;
    root["pwd"] = info->pwd;
    root["email"] = info->email;
    root["nick"] = info->nick;
    root["desc"] = info->desc;
    root["icon"] = info->icon;
    RedisManagerPool::GetInstance().Set(baseKey, root.toStyledString());
    LOG_INFO("[LogicSystem] GetBaseUserInfo from mysql, uid:%d, name:%s", uid, userInfo->name.c_str());
    return true;
}

// 支持uid查询
void LogicSystem::HandleSearchUser(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    LOG_DEBUG("[LogicSystem] HandleSearchUser, msgId:%u", msgId);
    Json::Reader reader;
    Json::Value src;
    reader.parse(msg, src);
    auto uidStr = src["uid"].asString();
    Json::Value rtValue;
    // 无论从哪个分支返回，统一发送查询响应
    Defer defer([&]() {
        session->SendMessage(rtValue.toStyledString(), static_cast<uint16_t>(MSG_IDS::SEARCH_USER_RSP));
    });

    // uid 必须为纯数字，避免 stoi 抛异常导致无法回复客户端
    if (uidStr.empty() || !std::all_of(uidStr.begin(), uidStr.end(), [](unsigned char c) { return std::isdigit(c); })) {
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        LOG_WARN("[LogicSystem] HandleSearchUser failed, invalid uid:%s", uidStr.c_str());
        return;
    }
    auto key = USER_INFO_PREFIX + uidStr;
    auto infoStr = RedisManagerPool::GetInstance().Get(key);
    if (infoStr.has_value()) {
        Json::Reader reader;
        reader.parse(infoStr.value(), rtValue);
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
        LOG_DEBUG("[LogicSystem] HandleSearchUser from redis, uid:%s", uidStr.c_str());
        return;
    }
    int32_t uid = std::stoi(uidStr);
    auto info = MysqlMganager::GetInstance().GetUserByUid(uid);
    if(!info) {
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        LOG_WARN("[LogicSystem] HandleSearchUser failed, uid:%s not found", uidStr.c_str());
        return;
    }
    rtValue["uid"] = uid;
    rtValue["sex"] = info->sex;
    rtValue["name"] = info->name;
    rtValue["pwd"] = info->pwd;
    rtValue["email"] = info->email;
    rtValue["nick"] = info->nick;
    rtValue["desc"] = info->desc;
    rtValue["icon"] = info->icon;
    RedisManagerPool::GetInstance().Set(key, rtValue.toStyledString());
    rtValue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    LOG_INFO("[LogicSystem] HandleSearchUser from mysql, uid:%d, name:%s", uid, info->name.c_str());
}

// from_uid ----> to_uid  apply_info name
void LogicSystem::HandleAddFriend(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg)
{
    LOG_DEBUG("[LogicSystem] HandleAddFriend, msgId:%u", msgId);
    Json::Reader reader;
    Json::Value src;
    reader.parse(msg, src);
    auto from_uid = src["from_uid"].asInt();
    auto to_uid = src["to_uid"].asInt();
    auto name = src["name"].asString();
    auto desc = src["desc"].asString();
    auto remark_name = src["remark_name"].asString();
    (void)remark_name;

    // 通知逻辑结束后统一给申请者回包
    Json::Value rtValue;
    rtValue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    Defer defer([&]() {
        session->SendMessage(rtValue.toStyledString(), static_cast<uint16_t>(MSG_IDS::ADD_FRIEND_RSP));
    });

    MysqlMganager::GetInstance().AddFriendApply(from_uid, to_uid);
    auto key = USER_SERVER_PREFIX + std::to_string(to_uid);
    auto to_server = RedisManagerPool::GetInstance().Get(key);
    if (!to_server.has_value()) {
        LOG_INFO("[LogicSystem] HandleAddFriend, to_uid:%d offline", to_uid);
        return;
    }
    auto self_server = ConfigManager::GetInstance().GetValue<std::string>("SelfServer", "Name");
    if (to_server.value() == self_server) {
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
    Json::Reader reader;
    Json::Value src;
    reader.parse(msg, src);
    auto peer_uid = src["from_uid"].asInt();   // 申请者
    auto self_uid = src["to_uid"].asInt();     // 处理者（本人）
    auto remark_name = src["remark_name"].asString();
    // 旧客户端不带 action，默认同意
    auto action = src.isMember("action") ? src["action"].asInt() : FRIEND_ACTION_ACCEPT;

    Json::Value rtValue;
    rtValue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    // 无论从哪个分支返回，统一给处理者回 AUTH_FRIEND_RSP
    Defer defer([&]() {
        session->SendMessage(rtValue.toStyledString(), static_cast<uint16_t>(MSG_IDS::AUTH_FRIEND_RSP));
    });

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
    if (!MysqlMganager::GetInstance().AuthFriendApply(peer_uid, self_uid, applyStatus)) {
        LOG_WARN("[LogicSystem] HandleAuthFriend update apply status failed, from:%d, to:%d, action:%d",
            peer_uid, self_uid, action);
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }

    // 2) 同意才建立好友关系；拒绝不建立
    if (action == FRIEND_ACTION_ACCEPT &&
        !MysqlMganager::GetInstance().AddFriend(peer_uid, self_uid, remark_name)) {
        LOG_WARN("[LogicSystem] HandleAuthFriend AddFriend failed, from:%d, to:%d", peer_uid, self_uid);
        rtValue["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return;
    }
    LOG_INFO("[LogicSystem] HandleAuthFriend %s, from:%d, to:%d",
        action == FRIEND_ACTION_ACCEPT ? "accepted" : "rejected", peer_uid, self_uid);

    // 3) 通知申请者处理结果（在线才通知，离线时其重新登录会拉取最新申请状态）
    auto key = USER_SERVER_PREFIX + std::to_string(peer_uid);
    auto peer_server = RedisManagerPool::GetInstance().Get(key);
    if (!peer_server.has_value()) {
        LOG_INFO("[LogicSystem] HandleAuthFriend, peer_uid:%d offline", peer_uid);
        return;
    }

    auto self_server = ConfigManager::GetInstance().GetValue<std::string>("SelfServer", "Name");
    if (peer_server.value() == self_server) {
        auto psession = UserManager::GetInstance().GetSession(peer_uid);
        if (!psession) {
            LOG_WARN("[LogicSystem] HandleAuthFriend, peer_uid:%d session not found", peer_uid);
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
    LOG_INFO("[LogicSystem] HandleAuthFriend rpc to server:%s, from:%d, to:%d, action:%d",
        peer_server.value().c_str(), peer_uid, self_uid, action);
    ChatGrpcClient::GetInstance().NotifyFriendAccepted(peer_server.value(), accept_req);
}

// 处理文本聊天消息 self_uid ---> peer_uid
void LogicSystem::HandleTextChatMsg(std::shared_ptr<CSession> session, const uint16_t msgId, const std::string& msg) {
    LOG_INFO("[LogicSystem] HandleTextChatMsg, msgId:%d, msg:%s", msgId, msg.c_str());
    Json::Reader reader;
    Json::Value src;
    reader.parse(msg, src);
    auto self_uid = src["from_uid"].asInt();   // 发送者
    auto peer_uid = src["to_uid"].asInt();     // 接收者
    const Json::Value array = src["text_array"];
    Json::Value rtvalue;
    rtvalue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    rtvalue["text_array"] = array;
    rtvalue["from_uid"] = self_uid;
    rtvalue["to_uid"] = peer_uid;

    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->SendMessage(return_str, static_cast<uint16_t>(MSG_IDS::TEXT_CHAT_MSG_RSP));
    });

    //查询redis 查找peer_uid对应的server ip
    auto peer_key = USER_SERVER_PREFIX + std::to_string(peer_uid);
    auto peer_server = RedisManagerPool::GetInstance().Get(peer_key);
    if (!peer_server.has_value()) {
        return;
    }

    auto self_server =  ConfigManager::GetInstance().GetValue<std::string>("SelfServer", "Name");
    //直接通知对方有文本聊天消息
    if (peer_server.value() == self_server) {
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
