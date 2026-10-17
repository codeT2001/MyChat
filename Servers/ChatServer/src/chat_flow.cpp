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
