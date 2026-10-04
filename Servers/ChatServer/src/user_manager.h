#ifndef _P1_USER_MANAGER_H_
#define _P1_USER_MANAGER_H_

#include <unordered_map>
#include <memory>
#include <mutex>
namespace P1 {
class CSession;
class UserManager final
{
public:
    static UserManager& GetInstance() {
        static UserManager instance;
        return instance;
    }
	~UserManager();
	std::shared_ptr<CSession> GetSession(int32_t uid);
	void SetUserSession(int32_t uid, std::shared_ptr<CSession> session);
	// 移除 uid 绑定的会话；仅当绑定的正是 session 时才移除并返回 true，
	// 用于区分"当前生效的会话断开"与"被顶掉的旧会话断开"
	bool RmvUserSession(int32_t uid, const std::shared_ptr<CSession>& session);
private:	
	UserManager();
	std::mutex session_mtx_;
	std::unordered_map<int32_t, std::shared_ptr<CSession>> uid_to_session_;
};
} // namespace P1
#endif // _P1_USER_MANAGER_H_