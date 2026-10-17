#include "chat_flow.h"

#include <algorithm>
#include <cctype>

#include "chat/log.h"

namespace P1 {

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
