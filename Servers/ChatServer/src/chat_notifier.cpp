#include "chat_notifier.h"

#include <memory>
#include <vector>

#include "chat/constants.h"
#include "chat/log.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
#include "csession.h"
#include "user_manager.h"

namespace P1 {

ChatNotifier::ChatNotifier()
{
    deps_.user_store = &MysqlManager::GetInstance();
    deps_.kv_store = &RedisManagerPool::GetInstance();
}

bool ChatNotifier::NotifyAddFriend(const message::AddFriendReq& req)
{
    auto session = UserManager::GetInstance().GetSession(req.to_uid());
    if (!session) {
        return false;
    }
    const Json::Value val = BuildPeerAddFriendNotify(req.apply_uid(), req.name(),
        req.desc(), req.icon(), req.sex(), req.nick());
    session->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_ADD_FRIEND_REQ));
    return true;
}

bool ChatNotifier::NotifyFriendAccepted(const message::FriendAcceptedReq& req)
{
    auto session = UserManager::GetInstance().GetSession(req.from_uid());
    if (!session) {
        return false;
    }

    const int32_t action = ResolvePeerAuthAction(req.action());
    std::shared_ptr<UserInfo> userInfo;
    if (action == FRIEND_ACTION_ACCEPT) {
        const std::string infoKey = std::string(USER_INFO_PREFIX) + std::to_string(req.to_uid());
        if (!LoadBaseUserInfo(infoKey, req.to_uid(), userInfo, deps_)) {
            LOG_ERROR("[ChatNotifier] NotifyFriendAccepted get user info failed, to_uid:%d", req.to_uid());
        }
    }
    const Json::Value val = BuildAuthResultNotify(req.from_uid(), req.to_uid(), action, userInfo.get());
    session->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_AUTH_FRIEND_REQ));
    return true;
}

bool ChatNotifier::NotifyTextChatMsg(const message::TextChatMsgReq& req)
{
    auto session = UserManager::GetInstance().GetSession(req.to_uid());
    if (!session) {
        return false;
    }

    std::vector<std::pair<std::string, std::string>> msgs;
    msgs.reserve(static_cast<size_t>(req.msgs_size()));
    for (const auto& msg : req.msgs()) {
        msgs.emplace_back(msg.msg_id(), msg.msg_content());
    }
    const Json::Value val = BuildPeerTextChatNotify(req.from_uid(), req.to_uid(), msgs);
    session->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_TEXT_CHAT_MSG_REQ));
    return true;
}

bool ChatNotifier::NotifyKickUser(const message::KickUserReq& req)
{
    auto session = UserManager::GetInstance().GetSession(req.uid());
    if (!session) {
        return false;
    }
    Json::Value val;
    val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    val["reason"] = "account logged in elsewhere";
    session->SendMessage(val.toStyledString(), static_cast<uint16_t>(MSG_IDS::NOTIFY_KICK));
    // 跨节点顶号也必须由服务端关闭旧连接，不能依赖客户端自觉断开。
    session->CloseAfterFlush();
    LOG_WARN("[ChatNotifier] kicked user, uid:%d, sessionId:%u", req.uid(), session->GetSessionId());
    return true;
}

} // namespace P1
