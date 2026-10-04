#include "listitembase.h"

ListItemBase::ListItemBase(QWidget *parent) : QWidget(parent), itemType_(INVALID_ITEM)
{
    // 普通 QWidget 子类默认不渲染 QSS 的 background/border，
    // 必须开启该属性，否则条目 QSS 里的 border-radius 圆角不生效
    setAttribute(Qt::WA_StyledBackground, true);
}

void ListItemBase::SetItemType(ListItemType itemType)
{
    itemType_ = itemType;
}

ListItemType ListItemBase::GetItemType() const
{
    return itemType_;
}
