#include "adduseritem.h"
#include "ui_adduseritem.h"
#include "utils.h"

AddUserItem::AddUserItem(QWidget *parent) : ListItemBase(parent), ui(new Ui::AddUserItem)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/adduseritem.qss");
    SetItemType(ListItemType::ADD_USER_TIP_ITEM);
}

AddUserItem::~AddUserItem()
{
    delete ui;
}
