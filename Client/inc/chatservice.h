#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <QObject>
#include <QByteArray>
#include <QJsonArray>
#include <memory>
#include <vector>
#include "constants.h"

struct TextChatData;

// 聊天业务服务层：承接文本/图片/文件消息的发送与接收。
// UI 只通过本服务与网络交互，不直接调用 TcpManager 单例。
class ChatService : public QObject {
    Q_OBJECT
public:
    static ChatService &GetInstance()
    {
        static ChatService instance;
        return instance;
    }

    // 发送文本聊天消息（text_array 元素: {content, msgid}）
    void SendTextChatMsg(int fromUid, int toUid, const QJsonArray &textArray);

signals:
    // 收到对端发来的文本消息（已写入 UserManager 好友历史）
    void SigTextChatMsgReceived(int fromUid, std::vector<std::shared_ptr<TextChatData>> msgs);

private slots:
    void OnMessageReceived(RequestId id, const QByteArray &data);

private:
    ChatService();
    ~ChatService() = default;
    ChatService(const ChatService &) = delete;
    ChatService &operator=(const ChatService &) = delete;
};

#endif // CHATSERVICE_H
