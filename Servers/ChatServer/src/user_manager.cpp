#include "user_manager.h"
#include <functional>
#include "csession.h"
namespace P1 {
UserManager::~UserManager() = default;

UserManager::UserManager() = default;

std::shared_ptr<CSession> UserManager::GetSession(int32_t uid)
{
    return sessions_.Get(uid);
}

void UserManager::SetUserSession(int32_t uid, std::shared_ptr<CSession> session)
{
    sessions_.Set(uid, std::move(session));
}

bool UserManager::RmvUserSession(int32_t uid, const std::shared_ptr<CSession>& session)
{
    return sessions_.RemoveIfMatch(uid, session);
}

std::mutex& UserManager::GetUidLock(int32_t uid)
{
    const std::size_t index = std::hash<int32_t> {}(uid) % uidLocks_.size();
    return uidLocks_[index];
}
} // namespace P1
