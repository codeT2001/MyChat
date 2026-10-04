#include "listitembase.h"

ListItemBase::ListItemBase(QWidget *parent) : QWidget(parent), itemType_(INVALID_ITEM) {}

void ListItemBase::SetItemType(ListItemType itemType)
{
    itemType_ = itemType;
}

ListItemType ListItemBase::GetItemType() const
{
    return itemType_;
}
