#ifndef _CHAT_CONSTANTS_H_
#define _CHAT_CONSTANTS_H_
#include <functional>
#include <string>
namespace P1 {

// Defer类
class Defer {
public:
    // 接受一个lambda表达式或者函数指针
    Defer(std::function<void()> func) : func_(func) {}

    // 析构函数中执行传入的函数
    ~Defer()
    {
        func_();
    }

private:
    std::function<void()> func_;
};

struct UserInfo {
    int32_t uid;
    int32_t sex;
    std::string name;
    std::string pwd;
    std::string email;
    std::string nick;
    std::string desc;
    std::string icon;
    std::string back;
};

struct ApplyInfo {
	ApplyInfo(int uid, std::string name, std::string desc,
		std::string icon, std::string nick, int _sex, int status)
		:uid(uid),name(name),desc(desc),
		icon(icon),nick(nick),sex(sex),status(status){}
	int uid;
    int sex;
	int status;
	std::string name;
	std::string desc;
	std::string icon;
	std::string nick;

};

// Redis key prefix for user tokens
constexpr char USER_TOKEN_PREFIX[] = "utoken_";
// Redis key prefix for user info
constexpr char USER_INFO_PREFIX[] = "uinfo_";
// Redis key prefix for user ip
constexpr char USER_SERVER_PREFIX[] = "userver_";
// Redis key for login count
constexpr char LOGIN_COUNT[] = "login_count";

// friend_apply.status：待处理 / 已同意 / 已拒绝
constexpr int32_t APPLY_STATUS_PENDING = 0;
constexpr int32_t APPLY_STATUS_ACCEPTED = 1;
constexpr int32_t APPLY_STATUS_REJECTED = 2;

// 客户端好友验证操作（AUTH_FRIEND_REQ 的 action 字段）
constexpr int32_t FRIEND_ACTION_ACCEPT = 1;
constexpr int32_t FRIEND_ACTION_REJECT = 2;
enum class ErrorCodes {
    SUCCESS = 0,
    ERROR_JSON = 1,
    RPC_FAILED = 2,
    VERIFYCODE_NOT_FOUND_OR_EXPIRED = 3,
    USER_EXIST = 4,
    EMAIL_NOT_MATCH = 5,
    UPDATE_PASSWORD_FAILED = 6,
    PASSWORD_NOT_MATCH = 7,
    UID_INVALID = 8,
    TOKEN_INVALID = 9,
    USER_ALREADY_LOGIN = 10,   // 账号已在其他连接登录，拒绝重复登录
};

// for chatserver
enum class MSG_IDS {
    CHAT_LOGIN_REQ = 10001,             // 登陆聊天服务器
    CHAT_LOGIN_RSP = 10002,             // 登陆聊天服务器回包
    SEARCH_USER_REQ = 10003,            // 搜索用户请求
    SEARCH_USER_RSP = 10004,            // 搜索用户响应
    ADD_FRIEND_REQ = 10005,             // 添加好友请求
    ADD_FRIEND_RSP = 10006,             // 添加好友响应
    NOTIFY_ADD_FRIEND_REQ = 10007,      // 通知添加好友请求
    NOTIFY_ADD_FRIEND_RSP = 10008,      // 通知添加好友响应
    AUTH_FRIEND_REQ = 10009,            // 好友认证请求
    AUTH_FRIEND_RSP = 10010,            // 好友认证响应
    NOTIFY_AUTH_FRIEND_REQ = 10011,     // 通知好友认证请求
    NOTIFY_AUTH_FRIEND_RSP = 10012,     // 通知好友认证响应
    TEXT_CHAT_MSG_REQ = 10013,          // 文本聊天消息请求
    TEXT_CHAT_MSG_RSP = 10014,          // 文本聊天消息响应
    NOTIFY_TEXT_CHAT_MSG_REQ = 10015,   // 通知文本聊天消息请求
    NOTIFY_TEXT_CHAT_MSG_RSP = 10016,   // 通知文本聊天消息响应
    CHAT_HEARTBEAT = 99999,             // 心跳包
};

} // namespace P1

#endif // _CHAT_CONSTANTS_H_
