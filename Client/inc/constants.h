#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <stdlib.h>
#include <QString>
#include <QPixmap>

enum class ErrorCodes {
    SUCCESS = 0,
    ERROR_JSON,
    RPC_FAILED,
    VERIFYCODE_NOT_FOUND_OR_EXPIRED,
    USER_EXIST,
    ERR_NETWORK,
    JSON_PARSE_ERROR = 5001,
};

enum class RequestId {
    // for GateServer
    GET_VERIFY_CODE = 1001, // 获取验证码
    REG_USER,               // 注册用户
    RESET_PASSWORD,         // 重置密码
    USER_LOGIN,             // 用户登录

    // forChatServer
    CHAT_LOGIN_REQ = 10001,         // 登陆聊天服务器
    CHAT_LOGIN_RSP = 10002,         // 登陆聊天服务器回包
    SEARCH_USER_REQ = 10003,        // 搜索用户请求
    SEARCH_USER_RSP = 10004,        // 搜索用户响应
    ADD_FRIEND_REQ = 10005,         // 添加好友请求
    ADD_FRIEND_RSP = 10006,         // 添加好友响应
    NOTIFY_ADD_FRIEND_REQ = 10007,  // 通知添加好友请求
    NOTIFY_ADD_FRIEND_RSP = 10008,  // 通知添加好友响应
    AUTH_FRIEND_REQ = 10009,        // 好友认证请求
    AUTH_FRIEND_RSP = 10010,        // 好友认证响应
    NOTIFY_AUTH_FRIEND_REQ = 10011, // 通知好友认证请求
    NOTIFY_AUTH_FRIEND_RSP = 10012, // 通知好友认证响应
    TEXT_CHAT_MSG_REQ = 10013,      // 文本聊天消息请求
    TEXT_CHAT_MSG_RSP = 10014,      // 文本聊天消息响应
    NOTIFY_TEXT_CHAT_MSG_REQ = 10015, // 通知文本聊天消息请求
    NOTIFY_TEXT_CHAT_MSG_RSP = 10016, // 通知文本聊天消息响应
    CHAT_HEARTBEAT = 99999, // 心跳包
};

// HTTP API path constants
namespace HttpPaths {
inline constexpr char GET_VERIFY_CODE[] = "/getVerifyCode";
inline constexpr char REGISTER_USER[] = "/registerUser";
inline constexpr char RESET_PASSWORD[] = "/resetPassword";
inline constexpr char USER_LOGIN[] = "/userLogin";
} // namespace HttpPaths

struct ServerInfo {
    uint16_t port;
    int32_t uid;
    QString host;
    QString token;
};

enum class MSG_IDS {
    CHAT_LOGIN = 10001,
    CHAT_LOGIN_RSP,
};

struct MsgInfo {
    QString msgFlag; //"text,image,file"
    QString content; // 表示文件和图像的url,文本信息
    QPixmap pixmap;  // 文件和图片的缩略图
};

#endif // CONSTANTS_H
