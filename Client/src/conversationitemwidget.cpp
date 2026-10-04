#include "conversationitemwidget.h"
#include "ui_conversationitemwidget.h"
#include "utils.h"
#include <QPixmap>

ConversationItemWidget::ConversationItemWidget(QWidget *parent) : ListItemBase(parent), ui(new Ui::ConversationItemWidget)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/conversationitemwidget.qss");
    ui->redPointLb->raise();
    SetShowRedPoint(false);
}

ConversationItemWidget::~ConversationItemWidget()
{
    delete ui;
}

void ConversationItemWidget::SetInfo(const QString &name, const QString &icon, const QString &msg)
{
    // 加载头像：ResolveIcon 统一解析协议 icon 字段，圆形化展示
    ui->iconLb->setPixmap(Utils::RoundedAvatar(icon, ui->iconLb->size()));
    ui->nameLb->setText(name);
    ui->msgLb->setText(msg);
}

void ConversationItemWidget::SetShowRedPoint(bool show)
{
    ui->redPointLb->setVisible(show);
}
