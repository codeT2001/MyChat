#include "chat/mysql_manager.h"
#include "chat/constants.h"
namespace P1 {

int MysqlMganager::RegisterUser(
    const std::string& name, const std::string& email, const std::string& pwd, const std::string& icon)
{
    return dao_.RegUserTransaction(name, email, pwd, icon);
}

bool MysqlMganager::CheckEmail(const std::string& name, const std::string & email)
{
    return dao_.CheckEmail(name, email);
}

bool MysqlMganager::UpdatePassword(const std::string& email, const std::string& pwd)
{
    return dao_.UpdatePassword(email, pwd);
}

bool MysqlMganager::CheckPassword(const std::string& name, const std::string& pwd, UserInfo& userInfo)
{
    return dao_.CheckPassword(name, pwd, userInfo);
}

std::shared_ptr<UserInfo> MysqlMganager::GetUserByUid(uint32_t uid)
{
    return dao_.GetUserByUid(uid);
}

std::shared_ptr<UserInfo> MysqlMganager::GetUserByName(const std::string& name)
{
    return dao_.GetUserByName(name);
}

bool MysqlMganager::AddFriendApply(const int from, const int to)
{
    return dao_.AddFriendApply(from, to);
}
bool MysqlMganager::AuthFriendApply(const int from, const int to, const int status)
{
    return dao_.AuthFriendApply(from, to, status);
}
bool MysqlMganager::AddFriend(const int from, const int to, const std::string& back_name)
{
    return dao_.AddFriend(from, to, back_name);
}

bool MysqlMganager::GetApplyList(int32_t uid, std::vector<std::shared_ptr<ApplyInfo>>& list, int begin, int limit)
{
    return dao_.GetApplyList(uid, list, begin, limit);
}
bool MysqlMganager::GetFriendList(int32_t uid, std::vector<std::shared_ptr<UserInfo> >& list)
{
    return dao_.GetFriendList(uid, list);
}
} // namespace P1
