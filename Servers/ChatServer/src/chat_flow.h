#ifndef _P1_CHAT_FLOW_H_
#define _P1_CHAT_FLOW_H_

#include <memory>
#include <vector>

#include "chat/constants.h"
#include "chat/service_interfaces.h"
#include "jsoncpp/json/json.h"

namespace P1 {

// ChatServer 业务逻辑所依赖的基础设施（接口指针）。
// 已接入「搜索用户」「登录」流程；其余 handler 仍直连单例，后续逐步迁移。
struct ChatDependencies {
    IUserStore* user_store = nullptr;
    IKeyValueStore* kv_store = nullptr;
};

// 搜索用户：只依赖入参 JSON 与接口，纯逻辑，返回完整响应 JSON。
Json::Value ProcessSearchUser(const Json::Value& req, ChatDependencies deps);

// 读取用户基础资料：先查 Redis 缓存，未命中回落 MySQL 并写回缓存（不写 pwd，带 TTL）。
// 成功后 userInfo 指向该用户资料；失败返回 false。
bool LoadBaseUserInfo(const std::string& baseKey, int32_t uid,
    std::shared_ptr<UserInfo>& userInfo, ChatDependencies deps);

// 登录流程的纯逻辑结果：response 为回包 JSON，ok 表示登录校验通过。
// 通过后的「路由覆盖 / 顶号 / 会话绑定」依赖 CSession 与节点信息，仍留在 Handler 里。
struct LoginResult {
    Json::Value response;
    bool ok = false;
    int32_t uid = 0;
    std::string name;
};

// 客户端 TCP 登录：校验 token（Redis）→ 拉取本人资料 → 组装资料 + 申请列表 + 好友列表。
LoginResult ProcessLogin(const Json::Value& req, ChatDependencies deps);

// 登录响应里的申请 / 好友列表序列化（纯函数，供 HandleLogin 复用）
void AppendApplyList(Json::Value& dst, const std::vector<std::shared_ptr<ApplyInfo>>& list);
void AppendFriendList(Json::Value& dst, const std::vector<std::shared_ptr<UserInfo>>& list);

} // namespace P1

#endif // _P1_CHAT_FLOW_H_
