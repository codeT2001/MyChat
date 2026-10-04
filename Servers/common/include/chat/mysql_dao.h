#ifndef _P1_MYSQL_DAO_H_
#define _P1_MYSQL_DAO_H_

#include <memory>
#include <string>
#include <vector>
namespace P1 {
struct UserInfo;
struct ApplyInfo;
class MysqlDao {
public:
    MysqlDao();
    ~MysqlDao();
    int RegUserTransaction(
        const std::string& name, const std::string& email, const std::string& pwd, const std::string& icon);
    bool CheckEmail(const std::string& name, const std::string & email);
    bool UpdatePassword(const std::string& email, const std::string& pwd);
    bool CheckPassword(const std::string& name, const std::string& pwd, UserInfo& userInfo);
    std::shared_ptr<UserInfo> GetUserByUid(uint32_t uid);
    std::shared_ptr<UserInfo> GetUserByName(const std::string& name);
    bool AddFriendApply(int32_t from, int32_t to);
	bool AuthFriendApply(int32_t from, int32_t to, int32_t status);
	bool AddFriend(int32_t from, int32_t to, const std::string& back_name);
    bool GetApplyList(int32_t uid, std::vector<std::shared_ptr<ApplyInfo>>& list, int32_t begin, int32_t limit = 10);
	bool GetFriendList(int32_t uid, std::vector<std::shared_ptr<UserInfo>>& list);
private:
    class SqlConnPool;
    std::unique_ptr<SqlConnPool> pool_;
};
} // namespace P1

#endif // _P1_MYSQL_DAO_H_