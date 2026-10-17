#ifndef _P1_USER_MANAGER_H_
#define _P1_USER_MANAGER_H_

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>

#include "chat/session_registry.h"
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
	// 同一 uid 的登录绑定与断开清理必须串行，避免路由/在线计数交叉更新。
	std::mutex& GetUidLock(int32_t uid);
private:	
	UserManager();
	SessionRegistry<std::shared_ptr<CSession>> sessions_;
	std::array<std::mutex, 64> uidLocks_;
};
} // namespace P1
#endif // _P1_USER_MANAGER_H_
