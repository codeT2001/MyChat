#ifndef JSONCODEC_H
#define JSONCODEC_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <vector>
#include "constants.h"

struct UserInfo;
struct SearchInfo;
struct AddFriendApply;
struct AcceptPeerInfo;
struct ApplyInfo;
struct FriendInfo;
struct ServerInfo;
struct TextChatData;

// JSON 处理层：仅负责 JSON ↔ 数据模型 的解析与序列化。
// 不依赖网络层、数据管理层、UI 层。

// 从 JSON 对象中提取 error 字段；缺失字段时返回 JSON_PARSE_ERROR。
ErrorCodes ExtractError(const QJsonObject &obj);

// AUTH_FRIEND_RSP 解析结果：action 区分同意/拒绝，info 仅同意时有效
struct AuthFriendRspResult {
    ErrorCodes error = ErrorCodes::JSON_PARSE_ERROR;
    int action = AcceptAction::NONE;
    std::shared_ptr<AcceptPeerInfo> info;
};

// NOTIFY_AUTH_FRIEND_REQ 解析结果：action 区分被同意/被拒绝，info 仅同意时有效
struct NotifyAuthFriendResult {
    ErrorCodes error = ErrorCodes::JSON_PARSE_ERROR;
    int action = AcceptAction::NONE;
    std::shared_ptr<AcceptPeerInfo> info;
};

// NOTIFY_TEXT_CHAT_MSG_REQ 解析结果：对端发来的文本消息列表
struct NotifyTextChatMsgResult {
    ErrorCodes error = ErrorCodes::JSON_PARSE_ERROR;
    int fromUid = 0;
    int toUid = 0;
    std::vector<std::shared_ptr<TextChatData>> msgs;
};

class JsonParser {
public:
    // 搜索用户响应
    static std::shared_ptr<SearchInfo> ParseSearchRsp(const QByteArray &data);
    // 收到好友申请通知
    static std::shared_ptr<AddFriendApply> ParseNotifyAddFriendReq(const QByteArray &data);
    // 收到对端发起的好友认证通知（含 action 标志位，区分同意/拒绝）
    static NotifyAuthFriendResult ParseNotifyAuthFriendReq(const QByteArray &data);
    // 自己发起的认证响应（含 action 标志位，区分同意/拒绝）
    static AuthFriendRspResult ParseAuthFriendRsp(const QByteArray &data);
    // 加好友响应（仅校验 error）
    static ErrorCodes ParseAddFriendRsp(const QByteArray &data);
    // 文本聊天消息响应（仅校验 error，成功表示消息已送达服务器）
    static ErrorCodes ParseTextChatMsgRsp(const QByteArray &data);
    // 收到对端发来的文本聊天消息通知
    static NotifyTextChatMsgResult ParseNotifyTextChatMsg(const QByteArray &data);

    // 从 QJsonObject 解析单个用户信息
    static std::shared_ptr<UserInfo> ParseUserInfo(const QJsonObject &obj);
    // 申请列表
    static std::vector<std::shared_ptr<ApplyInfo>> ParseApplyList(const QJsonArray &array);
    // 好友列表
    static std::vector<std::shared_ptr<FriendInfo>> ParseFriendList(const QJsonArray &array);

    // HTTP 登录响应（解析 uid/host/port/token）
    static ServerInfo ParseLoginHttpRsp(const QJsonObject &obj);
};

class JsonSerializer {
public:
    // 聊天服务器登录请求
    static QByteArray SerializeChatLoginReq(int uid, const QString &token);
    // 搜索用户请求
    static QByteArray SerializeSearchUserReq(const QString &uid);
    // 添加好友请求
    static QByteArray SerializeAddFriendReq(int fromUid,
                                            int toUid,
                                            const QString &name,
                                            const QString &desc,
                                            const QString &remarkName);
    // 好友认证请求（action: AcceptAction::ACCEPT=1 同意, REJECT=2 拒绝）
    static QByteArray SerializeAuthFriendReq(int fromUid,
                                             int toUid,
                                             const QString &name,
                                             const QString &desc,
                                             const QString &remarkName,
                                             int action);
    // 文本聊天消息请求（text_array 元素: {content, msgid}）
    static QByteArray SerializeTextChatMsgReq(int fromUid, int toUid, const QJsonArray &textArray);
};

#endif // JSONCODEC_H
