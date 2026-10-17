#include "user_manager.h"
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
} // namespace P1
