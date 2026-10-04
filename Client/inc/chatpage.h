#ifndef CHATPAGE_H
#define CHATPAGE_H

#include <QWidget>
#include <memory>

struct FriendInfo;

namespace Ui {
class ChatPage;
}

class ChatPage : public QWidget {
    Q_OBJECT

public:
    explicit ChatPage(QWidget *parent = nullptr);
    ~ChatPage();

    // 切换到某个好友的会话：更新标题、清空旧消息、渲染该好友历史消息
    void SetChatFriend(std::shared_ptr<FriendInfo> info);
    // 当前正在聊天的好友 uid，-1 表示未选择
    int GetCurrentUid() const { return currentUid_; }
    // 追加一条对端（对方）发来的消息到视图
    void AppendPeerMessage(const QString &name, const QString &icon, const QString &content);

protected:
    void paintEvent(QPaintEvent *event) override;
private slots:
    void OnSendMsgBtnClicked();

private:
    // 渲染一条聊天消息到 chatView
    void AppendMessage(bool isSelf, const QString &name, const QString &icon, const QString &content,
                       const QString &type);
    Ui::ChatPage *ui;
    int currentUid_ = -1; // 当前正在聊天的好友 uid，-1 表示未选择
};

#endif // CHATPAGE_H
