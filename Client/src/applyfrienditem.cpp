#include "applyfrienditem.h"
#include "ui_applyfrienditem.h"
#include "utils.h"
#include "userdata.h"
#include "log.h"
#include <QPushButton>
#include <QStyle>
ApplyFriendItem::ApplyFriendItem(QWidget *parent) : ListItemBase(parent), ui(new Ui::ApplyFriendItem)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/applyfrienditem.qss");
    // QWidget 子类必须开启此属性，QSS 的 background/border 才会生效
    setAttribute(Qt::WA_StyledBackground);
    SetItemType(ListItemType::APPLY_FRIEND_ITEM);
    ui->addFriendBtn->setCursor(Qt::PointingHandCursor);
    ui->addFriendBtn->hide();
    connect(ui->addFriendBtn, &QPushButton::clicked, this, [this]() {
        if (!info_) {
            return;
        }
        LOG_DEBUG() << info_->name_ << "clicked";
        emit this->sigAuthFriend(info_);
    });
}

ApplyFriendItem::~ApplyFriendItem()
{
    delete ui;
}

void ApplyFriendItem::SetInfo(std::shared_ptr<ApplyInfo> info)
{
    info_ = info;
    // 加载头像：ResolveIcon 统一解析协议 icon 字段，圆形化展示
    ui->iconLb->setPixmap(Utils::RoundedAvatar(info_->icon_, ui->iconLb->size()));

    ui->nameLb->setText(info_->name_);
    ui->msgLb->setText(info_->desc_);
}

void ApplyFriendItem::ShowAddBtn(bool bshow)
{
    if (bshow) {
        ui->addFriendBtn->show();
        ui->statusLb->hide();
        added_ = false;
    } else {
        ui->addFriendBtn->hide();
        ui->statusLb->setText(tr("已添加"));
        ui->statusLb->setProperty("state", "added");
        ui->statusLb->style()->unpolish(ui->statusLb);
        ui->statusLb->style()->polish(ui->statusLb);
        ui->statusLb->show();
        added_ = true;
    }
}

void ApplyFriendItem::ShowRejected()
{
    ui->addFriendBtn->hide();
    ui->statusLb->setText(tr("已拒绝"));
    ui->statusLb->setProperty("state", "rejected");
    ui->statusLb->style()->unpolish(ui->statusLb);
    ui->statusLb->style()->polish(ui->statusLb);
    ui->statusLb->show();
    added_ = true;
}

int ApplyFriendItem::GetUid()
{
    return info_ ? info_->uid_ : -1;
}
