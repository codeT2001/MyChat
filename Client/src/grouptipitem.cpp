#include "grouptipitem.h"
#include "ui_grouptipitem.h"

GroupTipItem::GroupTipItem(QWidget *parent) : ListItemBase(parent), groupName_(""), ui(new Ui::GroupTipItem)
{
    ui->setupUi(this);
    setEnabled(false);
    SetItemType(ListItemType::GROUP_TIP_ITEM);
}

GroupTipItem::~GroupTipItem()
{
    delete ui;
}

QSize GroupTipItem::sizeHint() const
{
    return QSize(250, 25); // 返回自定义的尺寸
}

void GroupTipItem::SetGroupTip(const QString &str)
{
    ui->label->setText(str);
}
