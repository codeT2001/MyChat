#ifndef CHATWINDOW_H
#define CHATWINDOW_H

#include <QWidget>
#include <QButtonGroup>
#include <memory>
#include <vector>

struct AddFriendApply;
struct FriendInfo;
struct TextChatData;
namespace Ui {
class ChatWindow;
}
enum class ChatMode {
    CHATS_MODE,
    SEARCH_MODE,
    CONTACTS_NODE,
};

enum class ListType {
    CHATS_LIST,
    SEARCH_LIST,
    CONTACTS_LIST,
};

class ChatWindow : public QWidget {
    Q_OBJECT

public:
    explicit ChatWindow(QWidget *parent = nullptr);
    ~ChatWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private slots:
    void OnSideChatBtnClicked();
    void OnSideContBtnClicked();
    void OnSearchEditTextChanged(const QString &text);
    void SlotFriendApply(std::shared_ptr<AddFriendApply> info);
    void SlotFriendAuth(std::shared_ptr<FriendInfo> info);
    void SlotSwitchFriendInfoPage(int uid);
    // 通讯录点击"自己"：显示当前登录用户资料
    void SlotSwitchSelfInfoPage();
    // 聊天列表点击某个会话：切换到该好友的聊天页
    void SlotChatItemClicked(int uid);
    // 收到对端发来的文本消息：若当前正与该好友聊天则追加到视图
    void SlotTextChatMsgReceived(int fromUid, std::vector<std::shared_ptr<TextChatData>> msgs);

private:
    void ShowSearchList(bool show);
    void HandleGlobalMousePress(QMouseEvent *event);
    void AddBadgeButtonToGroup();
    Ui::ChatWindow *ui;
    ChatMode mode_;
    ListType state_;
    QButtonGroup *buttonGroup_;
    // 通讯录模式下右侧最近显示的页面，切回时恢复
    QWidget *lastContactPage_ = nullptr;
};

#endif // CHATWINDOW_H
