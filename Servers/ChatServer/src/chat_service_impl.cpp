#include "chat_service_impl.h"

#include <jsoncpp/json/json.h>

#include "chat_flow.h"
#include "chat/constants.h"
#include "chat/log.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
#include "csession.h"
#include "user_manager.h"
namespace P1 {
ChatServiceImpl::ChatServiceImpl()
{
    // 复用 chat_flow 里的资料读取（Redis 缓存优先 + MySQL 回填 + TTL）
    deps_.user_store = &MysqlManager::GetInstance();
    deps_.kv_store = &RedisManagerPool::GetInstance();
}

ChatServiceImpl::~ChatServiceImpl() = default;
Status ChatServiceImpl::NotifyAddFriend(ServerContext* context, const AddFriendReq* request, AddFriendRsp* reply)
{
    // 查找用户是否在本服务器
    auto to_uid = request->to_uid();
    auto session = UserManager::GetInstance().GetSession(to_uid);

    reply->set_apply_uid(request->apply_uid());
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    reply->set_to_uid(to_uid);
    // 用户不在内存直接返回 
    // TODO 离线处理
    if (!session) {
        return Status::OK;
    }

    const Json::Value val = BuildPeerAddFriendNotify(request->apply_uid(), request->name(),
        request->desc(), request->icon(), request->sex(), request->nick());

    session->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_ADD_FRIEND_REQ));
    return Status::OK;
}

Status ChatServiceImpl::NotifyFriendAccepted(ServerContext* context, const FriendAcceptedReq* request, FriendAcceptedRsp* reply)
{
    // 查找用户是否在本服务器
    auto from_uid = request->from_uid();
    auto to_uid = request->to_uid();
    // action：1=同意 2=拒绝；旧消息默认值 0 按同意兼容
    const int32_t action = ResolvePeerAuthAction(request->action());
    auto session = UserManager::GetInstance().GetSession(from_uid);

    reply->set_from_uid(from_uid);
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    reply->set_to_uid(to_uid);
    // 用户不在内存直接返回
    // TODO 离线处理
    if (!session) {
        return Status::OK;
    }

    // 拒绝时不携带用户资料，客户端按 action=2 处理即可
    // 与同服通知共用同一个组装函数，避免两处字段/错误码不一致
    std::shared_ptr<UserInfo> userInfo;
    if (action == FRIEND_ACTION_ACCEPT) {
        const std::string infoKey = std::string(USER_INFO_PREFIX) + std::to_string(to_uid);
        if (!LoadBaseUserInfo(infoKey, to_uid, userInfo, deps_)) {
            // 与同服通知分支保持一致：资料拉取失败需告知客户端，避免建出残缺好友项
            LOG_ERROR("[ChatServiceImpl] NotifyFriendAccepted get user info failed, to_uid:%d", to_uid);
        }
    }
    const Json::Value val = BuildAuthResultNotify(from_uid, to_uid, action, userInfo.get());

    session->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_AUTH_FRIEND_REQ));
    return Status::OK;
}

Status ChatServiceImpl::NotifyTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* reply)
{
    //查找用户是否在本服务器
    auto to_uid = request->to_uid();
    auto session = UserManager::GetInstance().GetSession(to_uid);
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));

    //用户不在内存中则直接返回
    if (session == nullptr) {
        return Status::OK;
    }

    // 在内存中则直接发送通知对方
    std::vector<std::pair<std::string, std::string>> msgs;
    msgs.reserve(static_cast<size_t>(request->msgs_size()));
    for (const auto& msg : request->msgs()) {
        msgs.emplace_back(msg.msg_id(), msg.msg_content());
    }
    const Json::Value rtvalue = BuildPeerTextChatNotify(request->from_uid(), request->to_uid(), msgs);

    session->SendMessage(rtvalue.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_TEXT_CHAT_MSG_REQ));
    return Status::OK;
}

Status ChatServiceImpl::NotifyKickUser(ServerContext* context, const KickUserReq* request, KickUserRsp* reply)
{
    auto uid = request->uid();
    reply->set_uid(uid);
    auto session = UserManager::GetInstance().GetSession(uid);
    // 用户不在线（路由键残留、会话已断）视为无需处理，返回成功
    if (!session) {
        LOG_INFO("[ChatServiceImpl] NotifyKickUser, uid not on this server, uid:%d", uid);
        reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
        return Status::OK;
    }
    // 只发踢下线通知，不强制关 socket：客户端收到后自行断开，
    // 走正常断开清理链路（此时它已被新登录顶掉，不会误删 Redis 状态）；
    // 客户端僵死不读时由 TCP 超时兜底
    Json::Value val;
    val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    val["reason"] = "account logged in elsewhere";
    session->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_KICK));
    LOG_WARN("[ChatServiceImpl] kicked user, uid:%d, sessionId:%u", uid, session->GetSessionId());
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    return Status::OK;
}

bool ChatServiceImpl::GetBaseInfo(const std::string& baseKey, int32_t uid, std::shared_ptr<UserInfo>& userInfo)
{
	// Redis 缓存优先，未命中回落 MySQL 并写回缓存（实现见 chat_flow.cpp）
	return LoadBaseUserInfo(baseKey, uid, userInfo, deps_);
}
} // namespace P1
