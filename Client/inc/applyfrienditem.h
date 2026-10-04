#ifndef APPLYFRIENDITEM_H
#define APPLYFRIENDITEM_H

#include <QWidget>
#include "listitembase.h"
namespace Ui {
class ApplyFriendItem;
}
struct ApplyInfo;
class ApplyFriendItem : public ListItemBase {
    Q_OBJECT

public:
    explicit ApplyFriendItem(QWidget *parent = nullptr);
    ~ApplyFriendItem();
    void SetInfo(std::shared_ptr<ApplyInfo> apply_info);
    // 切换待处理 UI：pending=true 显示"添加"按钮；false 显示"已添加"状态
    void SetPendingUI(bool pending);
    void ShowRejected();
    QSize sizeHint() const override
    {
        return QSize(250, 80); // 返回自定义的尺寸
    }
    int GetUid();

private:
    Ui::ApplyFriendItem *ui;
    std::shared_ptr<ApplyInfo> info_;
    bool handled_ = false; // 该申请是否已处理（同意/拒绝）
signals:
    void SigAcceptFriend(std::shared_ptr<ApplyInfo> info);
};

#endif // APPLYFRIENDITEM_H
