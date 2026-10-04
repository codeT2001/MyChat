#ifndef CHATUSERLIST_H
#define CHATUSERLIST_H
#include <QListWidget>
#include <QMap>
#include <memory>

struct FriendInfo;
class QListWidgetItem;
class UserWidget;
class ChatUserList : public QListWidget {
    Q_OBJECT
public:
    explicit ChatUserList(QWidget *parent = nullptr);
    // 初始加载第一页数据（由外部在构造完成后调用一次）
    void LoadInitialUsers();
    // 外部插入单个用户（如新好友认证成功），自动去重并置顶
    void InsertUserItem(std::shared_ptr<FriendInfo> info);
    // 设置/取消某 uid 对应条目的红点
    void SetItemRedPoint(int uid, bool show);
    // 清空红点
    void ClearItemRedPoint(int uid);

signals:
    // 点击某个会话，参数为好友 uid（uid 存于 item 的 Qt::UserRole）
    void SigChatItemClicked(int uid);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void SlotItemClicked(QListWidgetItem *item);

private:
    void LoadMoreUsers(); // 触底加载下一页（含 loading 动画）
    void AddUserList();   // 从 UserManager 拉取一页并渲染
    void UpdateSelection(QListWidgetItem *clicked); // 切换条目选中态（配合 QSS selected 属性）
    UserWidget *FindItemWidget(QListWidgetItem *item) const; // 取条目内 UserWidget（兼容包裹容器）
    bool m_loadingPending = false;
    QMap<int, QListWidgetItem *> chatItemsAdded_; // uid → item，仅用于加载去重
};

#endif // CHATUSERLIST_H
