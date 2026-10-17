#ifndef CONSTANTS_H
#define CONSTANTS_H
#include <stdlib.h>
#include <QString>
#include <QPixmap>

// 错误码：数值即协议载荷，取值必须与 Servers/common/include/chat/constants.h 的 ErrorCodes
// 逐项同值（服务端回包只带数字）。契约由 Client/tests/test_protocol_contract.cpp 守住。
enum class ErrorCodes {
    SUCCESS = 0,
    ERROR_JSON = 1,
    RPC_FAILED = 2,
    VERIFYCODE_NOT_FOUND_OR_EXPIRED = 3,
    USER_EXIST = 4,
    EMAIL_NOT_MATCH = 5,         // 找回密码：姓名与邮箱不匹配
    UPDATE_PASSWORD_FAILED = 6,  // 找回密码：写入新密码失败
    PASSWORD_NOT_MATCH = 7,      // 登录：用户名或密码错误
    UID_INVALID = 8,             // 目标用户不存在 / uid 非法
    TOKEN_INVALID = 9,           // 聊天服务器 token 校验失败
    USER_ALREADY_LOGIN = 10,     // 账号已在其他连接登录
    // ===== VerifyServer 预留段（20001+）：与通用码隔离，与 Servers/VerifyServer/constants.js 保持一致 =====
    VERIFY_CODE_EXISTS = 20001, // 该邮箱 10 分钟内已有有效验证码，不重发
    VERIFY_REDIS_ERROR = 20002, // 验证码写入 Redis 失败
    VERIFY_MAIL_FAILED = 20003, // 验证码邮件发送失败
    // ===== 客户端本地预留段（5000+）：仅客户端内部产生，服务端不会返回 =====
    // 放在预留段是为了不与服务端错误码撞值：本地网络故障曾写成 5，与 EMAIL_NOT_MATCH 同值，
    // 导致「姓名与邮箱不匹配」被提示成「网络异常」
    ERR_NETWORK = 5000,      // HTTP 请求本身失败（无响应/超时）
    JSON_PARSE_ERROR = 5001, // 回包不是合法 JSON 或缺少 error 字段
};

enum class RequestId {
    // for GateServer
    GET_VERIFY_CODE = 1001, // 获取验证码
    REG_USER,               // 注册用户
    RESET_PASSWORD,         // 重置密码
    USER_LOGIN,             // 用户登录

    // forChatServer
    CHAT_LOGIN_REQ = 10001,         // 登录聊天服务器
    CHAT_LOGIN_RSP = 10002,         // 登录聊天服务器回包
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
    NOTIFY_KICK = 10017,              // 被顶号踢下线通知（服务端推送）
    CHAT_HEARTBEAT = 99999, // 心跳包
};

// HTTP API path constants
namespace HttpPaths {
inline constexpr char GET_VERIFY_CODE[] = "/getVerifyCode";
inline constexpr char REGISTER_USER[] = "/registerUser";
inline constexpr char RESET_PASSWORD[] = "/resetPassword";
inline constexpr char USER_LOGIN[] = "/userLogin";
} // namespace HttpPaths

// 好友认证（同意/拒绝）操作的 action 取值——线上协议载荷 jsonObj["action"]，数值冻结
namespace AcceptAction {
constexpr static int NONE = 0; // 默认值，不表示任何操作
constexpr static int ACCEPT = 1; // 同意
constexpr static int REJECT = 2; // 拒绝
} // namespace AcceptAction

// 文本消息批量限制。服务端单帧 body 硬上限为 2048 字节（ChatServer MAX_MSG_LENGTH），
// 超限帧会被服务端直接断开连接，所以分批一律按 UTF-8 字节数算（中文 1 字占 3 字节），
// 并把 JSON 键名/引号等包装开销一并计入，避免「多条短消息」的包装把 body 顶过上限。
namespace TextMsgLimits {
inline constexpr int MAX_FRAME_BODY_BYTES = 2048;           // 与服务端 MAX_MSG_LENGTH 对齐
inline constexpr int MAX_TEXT_BATCH_BYTES = 1024;           // 单批文本累计上限（含每条包装）
inline constexpr int MAX_SINGLE_MSG_BYTES = 1024;           // 单条消息内容上限（UTF-8 字节）
inline constexpr int PER_MSG_JSON_OVERHEAD_BYTES = 128;     // 每条：content/msg_id 键名 + QUuid 串
inline constexpr int BATCH_JSON_OVERHEAD_BYTES = 64;        // 整批：from_uid/to_uid/text_array 键名
} // namespace TextMsgLimits

struct ServerInfo {
    uint16_t port;
    int32_t uid;
    QString host;
    QString token;
};

struct MsgInfo {
    QString msgFlag; //"text,image,file"
    QString content; // 表示文件和图像的url,文本信息
    QPixmap pixmap;  // 文件和图片的缩略图
};

#endif // CONSTANTS_H
