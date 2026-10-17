#ifndef _CHAT_SESSION_REGISTRY_H_
#define _CHAT_SESSION_REGISTRY_H_

#include <mutex>
#include <unordered_map>

namespace P1 {

// uid → 会话指针 的线程安全注册表（纯逻辑，会话类型由模板参数决定，便于单测）。
// 关键语义：RemoveIfMatch 只在「当前绑定正是该会话」时移除，
// 否则不动——用于区分「当前生效的会话断开」与「被顶掉的旧会话断开」，
// 避免旧会话断连时误删新登录的绑定。
template <typename SessionPtr>
class SessionRegistry {
public:
    SessionPtr Get(int32_t uid) const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto iter = map_.find(uid);
        if (iter == map_.end()) {
            return SessionPtr {};
        }
        return iter->second;
    }

    void Set(int32_t uid, SessionPtr session)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        map_[uid] = std::move(session);
    }

    bool RemoveIfMatch(int32_t uid, const SessionPtr& session)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto iter = map_.find(uid);
        if (iter == map_.end() || !(iter->second == session)) {
            return false;
        }
        map_.erase(iter);
        return true;
    }

    size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return map_.size();
    }

private:
    mutable std::mutex mtx_;
    std::unordered_map<int32_t, SessionPtr> map_;
};

} // namespace P1

#endif // _CHAT_SESSION_REGISTRY_H_
