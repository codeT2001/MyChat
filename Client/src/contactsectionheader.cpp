#include "contactsectionheader.h"
#include "ui_contactsectionheader.h"

ContactSectionHeader::ContactSectionHeader(QWidget *parent) : ListItemBase(parent), groupName_(""), ui(new Ui::ContactSectionHeader)
{
    ui->setupUi(this);
    setEnabled(false);
    SetItemType(ListItemType::CONTACT_SECTION_ITEM);
}

ContactSectionHeader::~ContactSectionHeader()
{
    delete ui;
}

QSize ContactSectionHeader::sizeHint() const
{
    return QSize(250, 25); // 返回自定义的尺寸
}

void ContactSectionHeader::SetGroupTip(const QString &str)
{
    ui->label->setText(str);
}
