#ifndef _P1_MYSQL_MANAGER_H_
#define _P1_MYSQL_MANAGER_H_
#include "chat/mysql_dao.h"
#include "chat/noncopyable.h"
namespace P1 {
struct ApplyInfo;
struct UserInfo;
class MysqlMganager {
public:
    static MysqlMganager& GetInstance() { static MysqlMganager instance; return instance; }
    DISALLOW_COPY_MOVE(MysqlMganager);

    ~MysqlMganager() = default;

    int RegisterUser(const std::string& name, const std::string& email, const std::string& pwd, const std::string& icon);
    bool CheckEmail(const std::string& name, const std::string & email);
    bool UpdatePassword(const std::string& email, const std::string& pwd);
    bool CheckPassword(const std::string& name, const std::string& pwd, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUserByUid(uint32_t uid);
    std::shared_ptr<UserInfo> GetUserByName(const std::string& name);
    bool AddFriendApply(const int from, const int to);
	bool AuthFriendApply(const int from, const int to, const int status);
	bool AddFriend(const int from, const int to, const std::string& back_name);
    bool GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& list, int begin, int limit = 10);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo> >& list);
    // bool TestProcedure(const std::string &email, int& uid, const std::string & name);
private:
    MysqlMganager() = default;
    MysqlDao dao_;
};

} // namespace P1

#endif // _P1_MYSQL_MANAGER_H_