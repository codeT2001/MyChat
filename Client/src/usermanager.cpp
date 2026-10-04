#include "usermanager.h"
#include "domainmodels.h"
#include "logger.h"
#include <QtMath>
namespace {
constexpr int CHAT_COUNT_PER_PAGE = 13;
}
UserManager::~UserManager() {}

void UserManager::SetUserInfo(std::shared_ptr<UserInfo> info)
{
    userInfo_ = info;
}

std::shared_ptr<UserInfo> UserManager::GetUserInfo() const
{
    return userInfo_;
}

UserManager::UserManager() {}

void UserManager::SetToken(const QString &token)
{
    token_ = token;
}

void UserManager::AppendApplyList(const std::vector<std::shared_ptr<ApplyInfo>> &applyList)
{
    for (const auto &apply : applyList) {
        if (!apply) {
            continue;
        }
        applyList_[apply->uid_] = apply;
    }
}

void UserManager::AppendFriendList(const std::vector<std::shared_ptr<FriendInfo>> &friendList)
{
    for (const auto &info : friendList) {
        if (!info) {
            continue;
        }
        friendMap_[info->uid_] = info;
    }
}

int32_t UserManager::GetUid() const
{

    return userInfo_ ? userInfo_->uid_ : -1;
}

QString UserManager::GetName() const
{
    return userInfo_ ? userInfo_->name_ : "undefined";
}

QString UserManager::GetNick() const
{
    return userInfo_ ? userInfo_->nick_ : "undefined";
}

QString UserManager::GetIcon() const
{
    return userInfo_ ? userInfo_->icon_ : "undefined";
}

QString UserManager::GetDesc() const
{
    return userInfo_ ? userInfo_->desc_ : "undefined";
}

QString UserManager::GetToken() const
{
    return token_;
}

const std::unordered_map<int32_t, std::shared_ptr<ApplyInfo>> &UserManager::GetApplyList()
{
    return applyList_;
}

bool UserManager::AlreadyApply(int32_t uid)
{
    return applyList_.find(uid) != applyList_.end();
}

void UserManager::AddApply(std::shared_ptr<ApplyInfo> info)
{
    if (!AlreadyApply(info->uid_)) {
        applyList_[info->uid_] = info;
    }
}

std::vector<std::shared_ptr<FriendInfo>> UserManager::GetFriendsPerPage(int &loadedCount)
{
    std::vector<std::shared_ptr<FriendInfo>> list;

    const int total = static_cast<int>(friendMap_.size());
    int begin = loadedCount;

    if (begin >= total) {
        return list;
    }

    int end = begin + CHAT_COUNT_PER_PAGE;
    if (end > total) {
        end = total;
    }

    auto it = friendMap_.begin();
    std::advance(it, begin); // 移动到 begin 位置

    for (int i = begin; i < end && it != friendMap_.end(); ++i, ++it) {
        list.push_back(it->second); // map 的 value 是 shared_ptr<FriendInfo>
    }

    loadedCount = end;
    return list;
}

bool UserManager::IsFriendListExhausted(int loadedCount) const
{
    return loadedCount >= static_cast<int>(friendMap_.size());
}

bool UserManager::CheckFriendById(int uid)
{
    return friendMap_.find(uid) != friendMap_.end();
}

void UserManager::AddFriend(std::shared_ptr<AcceptPeerInfo> auth_info)
{
    auto info = std::make_shared<FriendInfo>(auth_info);
    friendMap_[info->uid_] = info;
}

void UserManager::AddFriend(std::shared_ptr<FriendInfo> friend_info)
{
    if (!friend_info) {
        return;
    }
    friendMap_[friend_info->uid_] = friend_info;
}

std::shared_ptr<FriendInfo> UserManager::GetFriendById(int32_t uid)
{
    auto it = friendMap_.find(uid);
    if (it == friendMap_.end()) {
        return nullptr;
    }
    return it->second;
}

void UserManager::UpdateFriendRemark(int32_t uid, const QString &remark)
{
    auto it = friendMap_.find(uid);
    if (it == friendMap_.end() || !it->second) {
        LOG_WARN() << "update friend remark failed, friend not found, uid =" << uid;
        return;
    }
    it->second->name_ = remark;
}

void UserManager::UpdateFriendLabel(int32_t uid, const QString &label)
{
    auto it = friendMap_.find(uid);
    if (it == friendMap_.end() || !it->second) {
        LOG_WARN() << "update friend label failed, friend not found, uid =" << uid;
        return;
    }
    it->second->label_ = label;
}

void UserManager::AppendFriendChatMsg(int friend_id, std::vector<std::shared_ptr<TextChatData>> msgs)
{
    auto it = friendMap_.find(friend_id);
    if (it == friendMap_.end()) {
        LOG_WARN() << "append friend uid  " << friend_id << " not found";
        return;
    }

    it->second->AppendChatMsgs(msgs);
}
