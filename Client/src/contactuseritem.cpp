#include "contactuseritem.h"
#include "ui_contactuseritem.h"
#include "utils.h"
#include "log.h"
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
    // 名称与头像解耦：头像失败不影响名称显示
    QPixmap pixmap(icon);
    if (pixmap.isNull()) {
        LOG_WARN() << "ContactUserItem: failed to load icon" << icon;
    } else {
        // 与 UserWidget 保持一致：先等比缩放，再由 setScaledContents 拉伸填满 label
        ui->iconLb->setPixmap(
            pixmap.scaled(ui->iconLb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        ui->iconLb->setScaledContents(true);
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
