#ifndef _CHAT_SERVICE_INTERFACES_H_
#define _CHAT_SERVICE_INTERFACES_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace P1 {
struct ApplyInfo;
struct UserInfo;

// 基础设施接口层：把业务逻辑与具体实现（MySQL / Redis / gRPC）解耦，
// 生产用真实实现，单测注入 fake。接口只收拢业务真正用到的子集，按需增补。

// 用户账号存储（MySQL 侧子集）
class IUserStore {
public:
    virtual ~IUserStore() = default;
    virtual int RegisterUser(const std::string& name, const std::string& email,
        const std::string& pwd, const std::string& icon) = 0;
    virtual bool CheckEmail(const std::string& name, const std::string& email) = 0;
    virtual bool UpdatePassword(const std::string& email, const std::string& pwd) = 0;
    virtual bool CheckPassword(const std::string& name, const std::string& pwd, UserInfo& userInfo) = 0;
    virtual std::shared_ptr<UserInfo> GetUserByUid(uint32_t uid) = 0;
    virtual bool GetApplyList(int32_t uid, std::vector<std::shared_ptr<ApplyInfo>>& list,
        int begin, int limit) = 0;
    virtual bool GetFriendList(int32_t uid, std::vector<std::shared_ptr<UserInfo>>& list) = 0;
    virtual bool AddFriendApply(int32_t from, int32_t to) = 0;
    virtual bool AuthFriendApply(int32_t from, int32_t to, int32_t status) = 0;
    virtual bool AddFriend(int32_t from, int32_t to, const std::string& backName) = 0;
};

// KV 存储（Redis 侧子集）
class IKeyValueStore {
public:
    virtual ~IKeyValueStore() = default;
    virtual void Set(const std::string& key, const std::string& val) = 0;
    virtual void SetEx(const std::string& key, const std::string& val, std::chrono::seconds ttl) = 0;
    virtual std::optional<std::string> Get(const std::string& key) = 0;
    virtual void HSet(const std::string& key, const std::string& field, const std::string& val) = 0;
    virtual std::optional<std::string> HGet(const std::string& key, const std::string& field) = 0;
    virtual std::unordered_map<std::string, std::string> HGetAll(const std::string& key) = 0;
    virtual std::optional<std::string> HIncrBy(const std::string& key, const std::string& field, int64_t increment) = 0;
    virtual bool Del(const std::string& key) = 0;
};

// 验证码服务（gRPC VerifyServer）：结果与 proto 解耦，便于 fake
struct VerifyCodeResult {
    int32_t error = 0;
};
class IVerifyCodeService {
public:
    virtual ~IVerifyCodeService() = default;
    virtual VerifyCodeResult GetVerifyCode(const std::string& email) = 0;
};

// 聊天服务器路由（gRPC StatusServer）：结果与 proto 解耦
struct ChatServerAssignment {
    int32_t error = 0;
    std::string host;
    std::string port;
    std::string token;
};
class IChatServerRouter {
public:
    virtual ~IChatServerRouter() = default;
    virtual ChatServerAssignment GetChatServer(int32_t uid) = 0;
};

} // namespace P1

#endif // _CHAT_SERVICE_INTERFACES_H_
