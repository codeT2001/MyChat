#include "user_manager.h"
#include "csession.h"
namespace P1 {
UserManager::~UserManager() = default;

UserManager::UserManager() = default;

std::shared_ptr<CSession> UserManager::GetSession(int32_t uid)
{
    std::lock_guard<std::mutex> lock(session_mtx_);
    auto iter = uid_to_session_.find(uid);
    if(iter == uid_to_session_.end()) {
        return nullptr;
    }
    return iter->second;
}
void UserManager::SetUserSession(int32_t uid, std::shared_ptr<CSession> session)
{        
    std::lock_guard<std::mutex> lock(session_mtx_);
    uid_to_session_[uid] = session;          
}

bool UserManager::RmvUserSession(int32_t uid, const std::shared_ptr<CSession>& session)
{
    std::lock_guard<std::mutex> lock(session_mtx_);
    auto iter = uid_to_session_.find(uid);
    if (iter == uid_to_session_.end() || iter->second != session) {
        return false;
    }
    uid_to_session_.erase(iter);
    return true;
}
} // namespace P1
