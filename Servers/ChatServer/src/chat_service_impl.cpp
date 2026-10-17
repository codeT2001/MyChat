#include "chat_service_impl.h"

#include "chat_flow.h"
#include "chat/constants.h"
#include "chat/mysql_manager.h"
#include "chat/redis_manager.h"
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
    reply->set_apply_uid(request->apply_uid());
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    reply->set_to_uid(request->to_uid());
    notifier_.NotifyAddFriend(*request);
    return Status::OK;
}

Status ChatServiceImpl::NotifyFriendAccepted(ServerContext* context, const FriendAcceptedReq* request, FriendAcceptedRsp* reply)
{
    reply->set_from_uid(request->from_uid());
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    reply->set_to_uid(request->to_uid());
    notifier_.NotifyFriendAccepted(*request);
    return Status::OK;
}

Status ChatServiceImpl::NotifyTextChatMsg(ServerContext* context, const TextChatMsgReq* request, TextChatMsgRsp* reply)
{
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    notifier_.NotifyTextChatMsg(*request);
    return Status::OK;
}

Status ChatServiceImpl::NotifyKickUser(ServerContext* context, const KickUserReq* request, KickUserRsp* reply)
{
    reply->set_uid(request->uid());
    notifier_.NotifyKickUser(*request);
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    return Status::OK;
}

bool ChatServiceImpl::GetBaseInfo(const std::string& baseKey, int32_t uid, std::shared_ptr<UserInfo>& userInfo)
{
	// Redis 缓存优先，未命中回落 MySQL 并写回缓存（实现见 chat_flow.cpp）
	return LoadBaseUserInfo(baseKey, uid, userInfo, deps_);
}
} // namespace P1
