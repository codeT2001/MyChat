#include "chat/mysql_manager.h"
#include "chat/constants.h"
namespace P1 {

int MysqlManager::RegisterUser(
    const std::string& name, const std::string& email, const std::string& pwd, const std::string& icon)
{
    return dao_.RegUserTransaction(name, email, pwd, icon);
}

bool MysqlManager::CheckEmail(const std::string& name, const std::string & email)
{
    return dao_.CheckEmail(name, email);
}

bool MysqlManager::UpdatePassword(const std::string& email, const std::string& pwd)
{
    return dao_.UpdatePassword(email, pwd);
}

bool MysqlManager::CheckPassword(const std::string& name, const std::string& pwd, UserInfo& userInfo)
{
    return dao_.CheckPassword(name, pwd, userInfo);
}

std::shared_ptr<UserInfo> MysqlManager::GetUserByUid(uint32_t uid)
{
    return dao_.GetUserByUid(uid);
}

std::shared_ptr<UserInfo> MysqlManager::GetUserByName(const std::string& name)
{
    return dao_.GetUserByName(name);
}

bool MysqlManager::AddFriendApply(const int from, const int to)
{
    return dao_.AddFriendApply(from, to);
}
bool MysqlManager::AuthFriendApply(const int from, const int to, const int status)
{
    return dao_.AuthFriendApply(from, to, status);
}
bool MysqlManager::AddFriend(const int from, const int to, const std::string& back_name)
{
    return dao_.AddFriend(from, to, back_name);
}

bool MysqlManager::GetApplyList(int32_t uid, std::vector<std::shared_ptr<ApplyInfo>>& list, int begin, int limit)
{
    return dao_.GetApplyList(uid, list, begin, limit);
}
bool MysqlManager::GetFriendList(int32_t uid, std::vector<std::shared_ptr<UserInfo> >& list)
{
    return dao_.GetFriendList(uid, list);
}
} // namespace P1
