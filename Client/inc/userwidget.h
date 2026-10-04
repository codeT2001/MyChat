#ifndef USERWIDGET_H
#define USERWIDGET_H

#include "listitembase.h"
namespace Ui {
class UserWidget;
}

// 会话列表条目：纯视图控件，只负责展示 头像/名称/最后一条消息，
// 不持有任何业务数据实体（uid 等由外部 QListWidget 条目映射维护）。
class UserWidget : public ListItemBase {
    Q_OBJECT

public:
    explicit UserWidget(QWidget *parent = nullptr);
    ~UserWidget();

    QSize sizeHint() const override
    {
        return QSize(250, 70);
    }

    void SetInfo(const QString &name, const QString &icon, const QString &msg);
    // 显示/隐藏未读消息红点
    void SetShowRedPoint(bool show);

private:
    Ui::UserWidget *ui;
};

#endif // USERWIDGET_H
