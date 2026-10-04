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
    void ShowAddBtn(bool bshow);
    void ShowRejected();
    QSize sizeHint() const override
    {
        return QSize(250, 80); // 返回自定义的尺寸
    }
    int GetUid();

private:
    Ui::ApplyFriendItem *ui;
    std::shared_ptr<ApplyInfo> info_;
    bool added_ = false;
signals:
    void sigAuthFriend(std::shared_ptr<ApplyInfo> info);
};

#endif // APPLYFRIENDITEM_H
