#ifndef _P1_CHAT_FLOW_H_
#define _P1_CHAT_FLOW_H_

#include <memory>
#include <vector>

#include "chat/constants.h"
#include "chat/service_interfaces.h"
#include "jsoncpp/json/json.h"

namespace P1 {

// ChatServer 业务逻辑所依赖的基础设施（接口指针）。
// 目前先接入「搜索用户」流程；其余 handler 仍直连单例，后续逐步迁移。
struct ChatDependencies {
    IUserStore* user_store = nullptr;
    IKeyValueStore* kv_store = nullptr;
};

// 搜索用户：只依赖入参 JSON 与接口，纯逻辑，返回完整响应 JSON。
Json::Value ProcessSearchUser(const Json::Value& req, ChatDependencies deps);

// 登录响应里的申请 / 好友列表序列化（纯函数，供 HandleLogin 复用）
void AppendApplyList(Json::Value& dst, const std::vector<std::shared_ptr<ApplyInfo>>& list);
void AppendFriendList(Json::Value& dst, const std::vector<std::shared_ptr<UserInfo>>& list);

} // namespace P1

#endif // _P1_CHAT_FLOW_H_
