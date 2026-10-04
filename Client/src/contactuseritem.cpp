#include "contactuseritem.h"
#include "ui_contactuseritem.h"
#include "utils.h"
#include "logger.h"
#include <QPixmap>
#include <QDebug>

ContactUserItem::ContactUserItem(QWidget *parent) : ListItemBase(parent), ui(new Ui::ContactUserItem)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/contactuseritem.qss");
    SetItemType(ListItemType::CONTACT_USER_ITEM);
    ui->redPointLb->raise();
    ShowRedPoint(false);
}

ContactUserItem::~ContactUserItem()
{
    delete ui;
}

QSize ContactUserItem::sizeHint() const
{
    return QSize(250, 70);
}

void ContactUserItem::SetInfo(const QString &name, const QString &icon)
{
    // 名称与头像解耦：头像失败不影响名称显示；ResolveIcon 统一解析协议 icon 字段
    QPixmap pixmap = Utils::RoundedAvatar(icon, ui->iconLb->size());
    if (pixmap.isNull()) {
        LOG_WARN() << "ContactUserItem: failed to load icon" << icon;
    } else {
        ui->iconLb->setPixmap(pixmap);
    }

    ui->nameLb->setText(name);
}

void ContactUserItem::ShowRedPoint(bool show)
{
    if (show) {
        ui->redPointLb->show();
    } else {
        ui->redPointLb->hide();
    }
}
