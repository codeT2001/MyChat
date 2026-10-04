#ifndef SEARCHENTRYITEM_H
#define SEARCHENTRYITEM_H

#include <QWidget>
#include "listitembase.h"
namespace Ui {
class SearchEntryItem;
}

class SearchEntryItem : public ListItemBase {
    Q_OBJECT

public:
    explicit SearchEntryItem(QWidget *parent = nullptr);
    ~SearchEntryItem();
    QSize sizeHint() const override
    {
        return QSize(250, 70); // 返回自定义的尺寸
    }

private:
    Ui::SearchEntryItem *ui;
};

#endif // SEARCHENTRYITEM_H
