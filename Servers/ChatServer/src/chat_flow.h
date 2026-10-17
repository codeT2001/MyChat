#ifndef _P1_CHAT_FLOW_H_
#define _P1_CHAT_FLOW_H_

#include <memory>
#include <string>
#include <utility>
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
// token 在成功校验时由 CompareAndDelete 原子消费；后续资料读取失败需重新登录获取 token。
// 通过后的「路由覆盖 / 顶号 / 会话绑定」依赖 CSession 与节点信息，仍留在 Handler 里。
struct LoginResult {
    Json::Value response;
    bool ok = false;
    int32_t uid = 0;
    std::string name;
};

// 客户端 TCP 登录：校验 token（Redis）→ 拉取本人资料 → 组装资料 + 申请列表 + 好友列表。
LoginResult ProcessLogin(const Json::Value& req, ChatDependencies deps);

// —— 好友申请 / 认证 / 文本消息（客户端可见的 JSON 契约集中在这里） ——

// 加好友申请：落库 + 给申请者的回包 + 给目标用户的本地通知。
// actorUid 必须来自已登录会话，请求中的 from_uid 必须与之一致。
// 跨节点通知所需的申请人资料（icon/sex/nick）由调用方在需要时再取，避免多余查询。
struct AddFriendPlan {
    Json::Value response;
    Json::Value notify;
    int32_t fromUid = 0;
    int32_t toUid = 0;
};
AddFriendPlan ProcessAddFriend(const Json::Value& req, int32_t actorUid, ChatDependencies deps);

// 认证请求的 action：缺省视为同意（兼容旧客户端）；非 1/2 返回 -1
int32_t ResolveAuthAction(const Json::Value& req);

// 认证动作 → 申请状态（同意 1 / 拒绝 2）
int32_t ApplyStatusForAction(int32_t action);

// 好友认证：校验请求中的 to_uid 必须是当前会话用户 → 校验 action →
// 取申请人资料 → 更新申请状态 →（同意时）建立好友关系。
// 通知申请者依赖会话/跨节点 RPC，留在 Handler 里。
struct AuthFriendPlan {
    Json::Value response;
    Json::Value notify;
    bool ok = false;
    int32_t peerUid = 0;
    int32_t selfUid = 0;
    int32_t action = 0;
};
AuthFriendPlan ProcessAuthFriend(const Json::Value& req, int32_t actorUid, ChatDependencies deps);

// 申请者视角的认证结果通知：同意时带上处理者资料（selfInfo 为空则 error=UID_INVALID）
Json::Value BuildAuthResultNotify(int32_t peerUid, int32_t selfUid, int32_t action,
    const UserInfo* selfInfo);

// 文本消息：回包 + 跨节点 gRPC 需要的 (msg_id, content) 列表。
// actorUid 必须来自已登录会话，请求中的 from_uid 必须与之一致。
struct TextChatPlan {
    Json::Value response;
    std::vector<std::pair<std::string, std::string>> msgs;
    int32_t fromUid = 0;
    int32_t toUid = 0;
};
TextChatPlan ProcessTextChatMsg(const Json::Value& req, int32_t actorUid);

// —— 跨节点（其他 ChatServer 通过 gRPC 发来）通知的 JSON 组装 ——

// 其他节点转发来的加好友申请 → 发给本节点的目标用户（带申请人资料）
Json::Value BuildPeerAddFriendNotify(int32_t applyUid, const std::string& name,
    const std::string& desc, const std::string& icon, int32_t sex, const std::string& nick);

// 跨节点认证消息里的 action：2=拒绝，其余（含旧消息默认 0）按同意兼容
int32_t ResolvePeerAuthAction(int32_t rawAction);

// 其他节点转发来的文本消息 → 发给本节点的接收者
Json::Value BuildPeerTextChatNotify(int32_t fromUid, int32_t toUid,
    const std::vector<std::pair<std::string, std::string>>& msgs);

// 登录响应里的申请 / 好友列表序列化（纯函数，供 HandleLogin 复用）
void AppendApplyList(Json::Value& dst, const std::vector<std::shared_ptr<ApplyInfo>>& list);
void AppendFriendList(Json::Value& dst, const std::vector<std::shared_ptr<UserInfo>>& list);

} // namespace P1

#endif // _P1_CHAT_FLOW_H_
