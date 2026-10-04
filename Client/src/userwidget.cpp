#include "userwidget.h"
#include "ui_userwidget.h"
#include "utils.h"
#include <QPixmap>

UserWidget::UserWidget(QWidget *parent) : ListItemBase(parent), ui(new Ui::UserWidget)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/userwidget.qss");
    ui->redPointLb->raise();
    SetShowRedPoint(false);
}

UserWidget::~UserWidget()
{
    delete ui;
}

void UserWidget::SetInfo(const QString &name, const QString &icon, const QString &msg)
{
    // 加载头像：ResolveIcon 统一解析协议 icon 字段，圆形化展示
    ui->iconLb->setPixmap(Utils::RoundedAvatar(icon, ui->iconLb->size()));
    ui->nameLb->setText(name);
    ui->msgLb->setText(msg);
}

void UserWidget::SetShowRedPoint(bool show)
{
    ui->redPointLb->setVisible(show);
}
