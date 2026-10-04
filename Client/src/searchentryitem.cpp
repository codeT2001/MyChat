#include "searchentryitem.h"
#include "ui_searchentryitem.h"
#include "utils.h"

SearchEntryItem::SearchEntryItem(QWidget *parent) : ListItemBase(parent), ui(new Ui::SearchEntryItem)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/searchentryitem.qss");
    SetItemType(ListItemType::SEARCH_ENTRY_ITEM);
}

SearchEntryItem::~SearchEntryItem()
{
    delete ui;
}
