#include "chat_service_impl.h"
#include "csession.h"
#include "chat/mysql_manager.h"
#include "user_manager.h"
#include <jsoncpp/json/value.h>
#include <jsoncpp/json/reader.h>
#include "chat/constants.h"
#include "chat/log.h"
#include "chat/redis_manager.h"
namespace P1 {

ChatServiceImpl::ChatServiceImpl() = default;

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

    Json::Value val;
    val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    val["apply_uid"] = request->apply_uid();
    val["name"] = request->name();
    val["desc"] = request->desc();
    val["icon"] = request->icon();
    val["sex"] = request->sex();
    val["nick"] = request->nick();

    std::string msg = val.toStyledString();

    session->SendMessage(msg, static_cast<uint16_t>(MSG_IDS::NOTIFY_ADD_FRIEND_REQ));
    return Status::OK;
}

Status ChatServiceImpl::NotifyFriendAccepted(ServerContext* context, const FriendAcceptedReq* request, FriendAcceptedRsp* reply)
{
    // 查找用户是否在本服务器
    auto from_uid = request->from_uid();
    auto to_uid = request->to_uid();
    // action：1=同意 2=拒绝；旧消息默认值 0 按同意兼容
    auto action = request->action() == static_cast<int32_t>(FRIEND_ACTION_REJECT)
        ? FRIEND_ACTION_REJECT : FRIEND_ACTION_ACCEPT;
    auto session = UserManager::GetInstance().GetSession(from_uid);

    reply->set_from_uid(from_uid);
    reply->set_error(static_cast<int32_t>(ErrorCodes::SUCCESS));
    reply->set_to_uid(to_uid);
    // 用户不在内存直接返回
    // TODO 离线处理
    if (!session) {
        return Status::OK;
    }

    Json::Value val;
    val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    val["action"] = action;
    val["from_uid"] = request->from_uid();
    val["to_uid"] = request->to_uid();

    // 拒绝时不携带用户资料，客户端按 action=2 处理即可
    if (action == FRIEND_ACTION_ACCEPT) {
        std::string info_key = USER_INFO_PREFIX + std::to_string(to_uid);
        auto user_info = std::make_shared<UserInfo>();
        if(GetBaseInfo(info_key, to_uid, user_info)) {
            val["name"] = user_info->name;
            val["desc"] = user_info->desc;
            val["icon"] = user_info->icon;
            val["sex"] = user_info->sex;
            val["nick"] = user_info->nick;
        }
        else {
            // 与同服通知分支保持一致：资料拉取失败需告知客户端，避免建出残缺好友项
            LOG_ERROR("[ChatServiceImpl] NotifyFriendAccepted get user info failed, to_uid:%d", to_uid);
            val["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        }
    }

    std::string msg = val.toStyledString();

    session->SendMessage(msg, static_cast<uint16_t>(MSG_IDS::NOTIFY_AUTH_FRIEND_REQ));
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

    //在内存中则直接发送通知对方
    Json::Value  rtvalue;
    rtvalue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    rtvalue["from_uid"] = request->from_uid();
    rtvalue["to_uid"] = request->to_uid();

    //将聊天数据组织为数组
    Json::Value text_array;
    for (auto& msg : request->msgs()) {
        Json::Value e;
        e["content"] = msg.msg_content();
        e["msg_id"] = msg.msg_id();    
        text_array.append(e);
    }
    rtvalue["text_array"] = text_array;

    std::string return_str = rtvalue.toStyledString();
    session->SendMessage(return_str, static_cast<uint16_t>(MSG_IDS::NOTIFY_TEXT_CHAT_MSG_REQ));
    return Status::OK;
}

bool ChatServiceImpl::GetBaseInfo(const std::string& baseKey, int32_t uid, std::shared_ptr<UserInfo>& userInfo)
{
	auto info = RedisManagerPool::GetInstance().Get(baseKey);
	if (info.has_value()) {
		Json::Reader reader;
		Json::Value root;
		reader.parse(info.value(), root);
		userInfo->uid = root["uid"].asInt();
		userInfo->name = root["name"].asString();
		userInfo->pwd = root["pwd"].asString();
		userInfo->email = root["email"].asString();
		userInfo->nick = root["nick"].asString();
		userInfo->desc = root["desc"].asString();
		userInfo->sex = root["sex"].asInt();
		userInfo->icon = root["icon"].asString();
        LOG_INFO("[ChatServiceImpl] GetBaseInfo from redis, uid:%d, name:%s", userInfo->uid, userInfo->name.c_str());
        return true;
	}

	userInfo = MysqlMganager::GetInstance().GetUserByUid(uid);
	if (!userInfo) {
		return false;
	}
	Json::Value val;
	val["uid"] = uid;
	val["pwd"] = userInfo->pwd;
	val["name"] = userInfo->name;
	val["email"] = userInfo->email;
	val["nick"] = userInfo->nick;
	val["desc"] = userInfo->desc;
	val["sex"] = userInfo->sex;
	val["icon"] = userInfo->icon;
	RedisManagerPool::GetInstance().Set(baseKey, val.toStyledString());
	return true;
}
} // namespace P1