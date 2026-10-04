#ifndef MAINPANEL_H
#define MAINPANEL_H

#include <QWidget>
#include <QButtonGroup>
#include <memory>
#include <vector>

struct AddFriendApply;
struct FriendInfo;
struct TextChatData;
namespace Ui {
class MainPanel;
}
// 主面板当前所处页面（搜索为覆盖态，退出搜索时依据侧边栏选中按钮回到聊天/通讯录）
enum class PageMode {
    ChatsPage,
    SearchPage,
    ContactsPage,
};

class MainPanel : public QWidget {
    Q_OBJECT

public:
    explicit MainPanel(QWidget *parent = nullptr);
    ~MainPanel();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private slots:
    void OnSideChatBtnClicked();
    void OnSideContBtnClicked();
    void OnSearchEditTextChanged(const QString &text);
    void OnFriendApply(std::shared_ptr<AddFriendApply> info);
    void OnFriendAccepted(std::shared_ptr<FriendInfo> info);
    void OnSwitchFriendInfoPage(int uid);
    // 通讯录点击"自己"：显示当前登录用户资料
    void OnSwitchSelfInfoPage();
    // 聊天列表点击某个会话：切换到该好友的聊天页
    void OnChatItemClicked(int uid);
    // 收到对端发来的文本消息：若当前正与该好友聊天则追加到视图
    void OnTextChatMsgReceived(int fromUid, std::vector<std::shared_ptr<TextChatData>> msgs);

private:
    void ShowSearchList(bool show);
    void HandleGlobalMousePress(QMouseEvent *event);
    void SetupSideButtonGroup();
    Ui::MainPanel *ui;
    PageMode pageMode_;
    QButtonGroup *buttonGroup_;
    // 通讯录模式下右侧最近显示的页面，切回时恢复
    QWidget *lastContactPage_ = nullptr;
};

#endif // MAINPANEL_H
