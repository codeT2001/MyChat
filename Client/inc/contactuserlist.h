#ifndef CONTACTUSERLIST_H
#define CONTACTUSERLIST_H

#include <QListWidget>
#include <QObject>
#include <QSet>
class ContactUserItem;
struct FriendInfo;
class ContactUserList : public QListWidget {
    Q_OBJECT
public:
    explicit ContactUserList(QWidget *parent = nullptr);
    void ShowRedPoint(bool bshow = true);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void InitList();       // 装配固定条目：分组标题、新的朋友、联系人分组
    void AddContactList(); // 从 UserManager 拉取一页联系人并渲染
    void LoadMoreUsers();  // 触底加载下一页（含 loading 动画 + 定时器防抖）

public slots:
    void SlotItemClicked(QListWidgetItem *item);
    void SlotFriendAuth(std::shared_ptr<FriendInfo> info);
signals:
    void SigSwitchApplyFriendPage();
    void SigSwitchSelfInfoPage();
    void SigSwitchFriendInfoPage(int uid);

private:
    ContactUserItem *newFriendItem_ = nullptr;
    QListWidgetItem *groupItem_ = nullptr;
    bool m_loadingPending = false;
    // 已添加到通讯录列表的 uid，用于本地去重（不依赖 UserManager 状态）
    QSet<int> addedUids_;
};

#endif // CONTACTUSERLIST_H
