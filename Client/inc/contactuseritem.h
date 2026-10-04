#ifndef CONTACTUSERITEM_H
#define CONTACTUSERITEM_H

#include <QWidget>
#include "listitembase.h"
namespace Ui {
class ContactUserItem;
}

// 通讯录条目：纯视图控件，只负责渲染名称/头像，
// 条目标识（类型/uid）由所属 QListWidgetItem 的 UserRole 维护。
class ContactUserItem : public ListItemBase {
    Q_OBJECT

public:
    explicit ContactUserItem(QWidget *parent = nullptr);
    ~ContactUserItem();
    QSize sizeHint() const override;
    void SetInfo(const QString &name, const QString &icon);
    void ShowRedPoint(bool show = false);

private:
    Ui::ContactUserItem *ui;
};

#endif // CONTACTUSERITEM_H
