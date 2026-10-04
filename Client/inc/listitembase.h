#ifndef LISTITEMBASE_H
#define LISTITEMBASE_H

#include <QWidget>

enum ListItemType {
    CHAT_USER_ITEM,
    CONTACT_USER_ITEM,
    SELF_USER_ITEM,
    GROUP_TIP_ITEM,
    SEARCH_USER_ITEM,
    ADD_USER_TIP_ITEM,
    INVALID_ITEM,
    LINE_ITEM,
    APPLY_FRIEND_ITEM,
};

class ListItemBase : public QWidget {
    Q_OBJECT
public:
    explicit ListItemBase(QWidget *parent = nullptr);
    void SetItemType(ListItemType itemType);

    ListItemType GetItemType() const;
public slots:

signals:
private:
    ListItemType itemType_;
};

#endif // LISTITEMBASE_H
