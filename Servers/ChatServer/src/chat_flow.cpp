#include "chat_flow.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>

#include "chat/log.h"

namespace P1 {

// uinfo_ 用户资料缓存的统一 TTL：防止资料变更后脏数据永久驻留
constexpr auto USER_INFO_CACHE_TTL = std::chrono::seconds(3600);

Json::Value ProcessSearchUser(const Json::Value& req, ChatDependencies deps)
{
    Json::Value rsp;
    const std::string uidStr = req["uid"].asString();

    // uid 必须为纯数字，避免 stoi 抛异常导致无法回复客户端
    if (uidStr.empty() ||
        !std::all_of(uidStr.begin(), uidStr.end(), [](unsigned char c) { return std::isdigit(c); })) {
        rsp["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        LOG_WARN("[LogicSystem] HandleSearchUser failed, invalid uid:%s", uidStr.c_str());
        return rsp;
    }

    const std::string key = USER_INFO_PREFIX + uidStr;
    if (deps.kv_store != nullptr) {
        auto infoStr = deps.kv_store->Get(key);
        if (infoStr.has_value()) {
            Json::Reader reader;
            reader.parse(infoStr.value(), rsp);
            rsp["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
            LOG_DEBUG("[LogicSystem] HandleSearchUser from redis, uid:%s", uidStr.c_str());
            return rsp;
        }
    }

    const int32_t uid = std::stoi(uidStr);
    auto info = (deps.user_store == nullptr)
        ? nullptr
        : deps.user_store->GetUserByUid(static_cast<uint32_t>(uid));
    if (!info) {
        rsp["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        LOG_WARN("[LogicSystem] HandleSearchUser failed, uid:%s not found", uidStr.c_str());
        return rsp;
    }
    rsp["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    rsp["uid"] = uid;
    rsp["sex"] = info->sex;
    rsp["name"] = info->name;
    rsp["email"] = info->email;
    rsp["nick"] = info->nick;
    rsp["desc"] = info->desc;
    rsp["icon"] = info->icon;
    LOG_INFO("[LogicSystem] HandleSearchUser ok, uid:%d, name:%s", uid, info->name.c_str());
    return rsp;
}

bool LoadBaseUserInfo(const std::string& baseKey, int32_t uid,
    std::shared_ptr<UserInfo>& userInfo, ChatDependencies deps)
{
    // 先查 Redis
    if (deps.kv_store != nullptr) {
        auto infoValue = deps.kv_store->Get(baseKey);
        if (infoValue.has_value()) {
            Json::Reader reader;
            Json::Value root;
            reader.parse(infoValue.value(), root);
            if (!userInfo) {
                userInfo = std::make_shared<UserInfo>();
            }
            userInfo->uid = root["uid"].asInt();
            userInfo->sex = root["sex"].asInt();
            userInfo->name = root["name"].asString();
            userInfo->email = root["email"].asString();
            userInfo->nick = root["nick"].asString();
            userInfo->desc = root["desc"].asString();
            userInfo->icon = root["icon"].asString();
            LOG_DEBUG("[LogicSystem] GetBaseUserInfo from redis, uid:%d, name:%s", uid, userInfo->name.c_str());
            return true;
        }
    }
    auto info = (deps.user_store == nullptr) ? nullptr : deps.user_store->GetUserByUid(uid);
    if (!info) {
        return false;
    }
    userInfo = info;
    if (deps.kv_store == nullptr) {
        return true;
    }
    Json::Value root;
    root["uid"] = uid;
    root["sex"] = info->sex;
    root["name"] = info->name;
    root["email"] = info->email;
    root["nick"] = info->nick;
    root["desc"] = info->desc;
    root["icon"] = info->icon;
    // 注意：缓存中不写 pwd——敏感数据不落 Redis；带 TTL，资料变更后最多 1 小时自愈
    deps.kv_store->SetEx(baseKey, root.toStyledString(), USER_INFO_CACHE_TTL);
    LOG_DEBUG("[LogicSystem] GetBaseUserInfo from mysql, uid:%d, name:%s", uid, userInfo->name.c_str());
    return true;
}

LoginResult ProcessLogin(const Json::Value& req, ChatDependencies deps)
{
    LoginResult result;
    Json::Value& rsp = result.response;
    const int32_t uid = req["uid"].asInt();
    const std::string token = req["token"].asString();
    const std::string uidStr = std::to_string(uid);

    // 查询 token 是否正确
    const std::string tokenKey = std::string(USER_TOKEN_PREFIX) + uidStr;
    auto tokenValue = (deps.kv_store == nullptr)
        ? std::optional<std::string> {}
        : deps.kv_store->Get(tokenKey);
    if (!tokenValue.has_value()) {
        rsp["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        LOG_ERROR("[LogicSystem] login failed, token not found in redis, uid:%d", uid);
        return result;
    }
    if (*tokenValue != token) {
        rsp["error"] = static_cast<int32_t>(ErrorCodes::TOKEN_INVALID);
        LOG_ERROR("[LogicSystem] login failed, token invalid, uid:%d", uid);
        return result;
    }

    std::shared_ptr<UserInfo> userInfo;
    const std::string baseKey = std::string(USER_INFO_PREFIX) + uidStr;
    if (!LoadBaseUserInfo(baseKey, uid, userInfo, deps)) {
        rsp["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        LOG_ERROR("[LogicSystem] login failed, uid not found, uid:%d", uid);
        return result;
    }

    rsp["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    rsp["uid"] = uid;
    rsp["sex"] = userInfo->sex;
    rsp["name"] = userInfo->name;
    rsp["email"] = userInfo->email;
    rsp["nick"] = userInfo->nick;
    rsp["desc"] = userInfo->desc;
    rsp["icon"] = userInfo->icon;
    rsp["token"] = token;

    // 获取申请列表
    std::vector<std::shared_ptr<ApplyInfo>> applyList;
    if (deps.user_store != nullptr && deps.user_store->GetApplyList(uid, applyList, 0, 10)) {
        AppendApplyList(rsp, applyList);
    }

    // 获取好友列表
    std::vector<std::shared_ptr<UserInfo>> friendList;
    if (deps.user_store != nullptr) {
        deps.user_store->GetFriendList(uid, friendList);
    }
    AppendFriendList(rsp, friendList);

    result.ok = true;
    result.uid = uid;
    result.name = userInfo->name;
    return result;
}

// —— 好友申请 / 认证 / 文本消息 ——

AddFriendPlan ProcessAddFriend(const Json::Value& req, ChatDependencies deps)
{
    AddFriendPlan plan;
    const int32_t fromUid = req["from_uid"].asInt();
    const int32_t toUid = req["to_uid"].asInt();
    const std::string name = req["name"].asString();
    const std::string desc = req["desc"].asString();

    plan.fromUid = fromUid;
    plan.toUid = toUid;
    plan.response["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);

    if (deps.user_store != nullptr) {
        deps.user_store->AddFriendApply(fromUid, toUid);
    }
    // 目标用户本节点在线时直接收通知（字段与旧版本保持一致）
    Json::Value& notify = plan.notify;
    notify["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    notify["desc"] = desc;
    notify["name"] = name;
    notify["apply_uid"] = fromUid;
    return plan;
}

int32_t ResolveAuthAction(const Json::Value& req)
{
    // 旧客户端不带 action，默认同意
    if (!req.isMember("action")) {
        return FRIEND_ACTION_ACCEPT;
    }
    // 非数字（字符串 / 布尔 / null / 对象）时 asInt() 会抛异常或按 0/1 误判，
    // 直接判为非法 action：否则异常会冲出 handler，客户端收不到任何回包
    const Json::Value& action = req["action"];
    if (!action.isNumeric()) {
        return -1;
    }
    const int32_t value = action.asInt();
    if (value != FRIEND_ACTION_ACCEPT && value != FRIEND_ACTION_REJECT) {
        return -1;
    }
    return value;
}

int32_t ApplyStatusForAction(int32_t action)
{
    return (action == FRIEND_ACTION_ACCEPT) ? APPLY_STATUS_ACCEPTED : APPLY_STATUS_REJECTED;
}

AuthFriendPlan ProcessAuthFriend(const Json::Value& req, ChatDependencies deps)
{
    AuthFriendPlan plan;
    const int32_t peerUid = req["from_uid"].asInt();  // 申请者
    const int32_t selfUid = req["to_uid"].asInt();    // 处理者（本人）
    const std::string remarkName = req["remark_name"].asString();

    plan.peerUid = peerUid;
    plan.selfUid = selfUid;
    plan.response["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);

    const int32_t action = ResolveAuthAction(req);
    if (action < 0) {
        LOG_WARN("[LogicSystem] HandleAuthFriend invalid action, from:%d, to:%d", peerUid, selfUid);
        plan.response["error"] = static_cast<int32_t>(ErrorCodes::ERROR_JSON);
        return plan;
    }
    plan.action = action;

    // 响应里需要带【申请人】的 id/资料，客户端据此在申请列表中定位并更新该条申请
    std::shared_ptr<UserInfo> userInfo;
    const std::string peerKey = std::string(USER_INFO_PREFIX) + std::to_string(peerUid);
    if (!LoadBaseUserInfo(peerKey, peerUid, userInfo, deps)) {
        plan.response["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return plan;
    }
    plan.response["uid"] = peerUid;
    plan.response["from_uid"] = peerUid;  // 申请人
    plan.response["to_uid"] = selfUid;    // 处理者（本人）
    plan.response["name"] = userInfo->name;
    plan.response["nick"] = userInfo->nick;
    plan.response["icon"] = userInfo->icon;
    plan.response["sex"] = userInfo->sex;
    plan.response["action"] = action;

    if (deps.user_store == nullptr) {
        plan.response["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return plan;
    }

    // 1) 同步申请状态：同意置 1，拒绝置 2
    const int32_t applyStatus = ApplyStatusForAction(action);
    if (!deps.user_store->AuthFriendApply(peerUid, selfUid, applyStatus)) {
        LOG_WARN("[LogicSystem] HandleAuthFriend update apply status failed, from:%d, to:%d, action:%d",
            peerUid, selfUid, action);
        plan.response["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return plan;
    }

    // 2) 同意才建立好友关系；拒绝不建立
    if (action == FRIEND_ACTION_ACCEPT &&
        !deps.user_store->AddFriend(peerUid, selfUid, remarkName)) {
        LOG_WARN("[LogicSystem] HandleAuthFriend AddFriend failed, from:%d, to:%d", peerUid, selfUid);
        plan.response["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        return plan;
    }
    LOG_INFO("[LogicSystem] HandleAuthFriend %s, from:%d, to:%d",
        action == FRIEND_ACTION_ACCEPT ? "accepted" : "rejected", peerUid, selfUid);

    // 通知内容（发给申请者）：同意时带【处理者本人】的资料，拒绝时只需 id + action
    if (action == FRIEND_ACTION_ACCEPT) {
        std::shared_ptr<UserInfo> selfInfo;
        const std::string selfKey = std::string(USER_INFO_PREFIX) + std::to_string(selfUid);
        const bool loaded = LoadBaseUserInfo(selfKey, selfUid, selfInfo, deps);
        plan.notify = BuildAuthResultNotify(peerUid, selfUid, action, loaded ? selfInfo.get() : nullptr);
    } else {
        plan.notify = BuildAuthResultNotify(peerUid, selfUid, action, nullptr);
    }
    plan.ok = true;
    return plan;
}

Json::Value BuildAuthResultNotify(int32_t peerUid, int32_t selfUid, int32_t action,
    const UserInfo* selfInfo)
{
    Json::Value val;
    val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    val["action"] = action;
    // 统一语义：from_uid=申请人（即接收通知者自己），to_uid=处理者
    val["from_uid"] = peerUid;
    val["to_uid"] = selfUid;
    // 同意才携带处理者资料（客户端据此建好友会话）；拒绝只需 id + action
    if (action == FRIEND_ACTION_ACCEPT) {
        if (selfInfo == nullptr) {
            val["error"] = static_cast<int32_t>(ErrorCodes::UID_INVALID);
        } else {
            val["name"] = selfInfo->name;
            val["desc"] = selfInfo->desc;
            val["nick"] = selfInfo->nick;
            val["icon"] = selfInfo->icon;
            val["sex"] = selfInfo->sex;
        }
    }
    return val;
}

TextChatPlan ProcessTextChatMsg(const Json::Value& req)
{
    TextChatPlan plan;
    const int32_t selfUid = req["from_uid"].asInt();  // 发送者
    const int32_t peerUid = req["to_uid"].asInt();    // 接收者
    const Json::Value array = req["text_array"];
    plan.fromUid = selfUid;
    plan.toUid = peerUid;
    plan.response["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    plan.response["text_array"] = array;
    plan.response["from_uid"] = selfUid;
    plan.response["to_uid"] = peerUid;

    if (array.isArray()) {
        plan.msgs.reserve(array.size());
        for (const auto& e : array) {
            plan.msgs.emplace_back(e["msg_id"].asString(), e["content"].asString());
        }
    }
    return plan;
}

Json::Value BuildPeerAddFriendNotify(int32_t applyUid, const std::string& name,
    const std::string& desc, const std::string& icon, int32_t sex, const std::string& nick)
{
    Json::Value val;
    val["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    val["apply_uid"] = applyUid;
    val["name"] = name;
    val["desc"] = desc;
    val["icon"] = icon;
    val["sex"] = sex;
    val["nick"] = nick;
    return val;
}

int32_t ResolvePeerAuthAction(int32_t rawAction)
{
    // 旧消息不带 action（默认 0）时按同意兼容
    return (rawAction == FRIEND_ACTION_REJECT) ? FRIEND_ACTION_REJECT : FRIEND_ACTION_ACCEPT;
}

Json::Value BuildPeerTextChatNotify(int32_t fromUid, int32_t toUid,
    const std::vector<std::pair<std::string, std::string>>& msgs)
{
    Json::Value rtvalue;
    rtvalue["error"] = static_cast<int32_t>(ErrorCodes::SUCCESS);
    rtvalue["from_uid"] = fromUid;
    rtvalue["to_uid"] = toUid;

    // 与旧实现一致：无消息时 text_array 为 null（客户端按 size() 读取，行为不变）
    Json::Value textArray;
    for (const auto& [msgId, content] : msgs) {
        Json::Value e;
        e["content"] = content;
        e["msg_id"] = msgId;
        textArray.append(e);
    }
    rtvalue["text_array"] = textArray;
    return rtvalue;
}

void AppendApplyList(Json::Value& dst, const std::vector<std::shared_ptr<ApplyInfo>>& list)
{
    for (const auto& apply : list) {
        Json::Value obj;
        obj["name"] = apply->name;
        obj["uid"] = apply->uid;
        obj["icon"] = apply->icon;
        obj["nick"] = apply->nick;
        obj["sex"] = apply->sex;
        obj["desc"] = apply->desc;
        obj["status"] = apply->status;
        dst["apply_list"].append(obj);
    }
}

void AppendFriendList(Json::Value& dst, const std::vector<std::shared_ptr<UserInfo>>& list)
{
    for (const auto& f : list) {
        Json::Value obj;
        obj["name"] = f->name;
        obj["uid"] = f->uid;
        obj["icon"] = f->icon;
        obj["nick"] = f->nick;
        obj["sex"] = f->sex;
        obj["desc"] = f->desc;
        obj["back"] = f->back;
        dst["friend_list"].append(obj);
    }
}

} // namespace P1
