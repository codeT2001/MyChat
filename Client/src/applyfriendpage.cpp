#include "applyfriendpage.h"
#include "ui_applyfriendpage.h"
#include "utils.h"
#include "userdata.h"
#include "friendservice.h"
#include "applyfrienditem.h"
#include "usermanager.h"
#include "applyfrienddialog.h"
#include "log.h"
#include <QPaintEvent>
#include <QPainter>

ApplyFriendPage::ApplyFriendPage(QWidget *parent) : QWidget(parent), ui(new Ui::ApplyFriendPage)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/applyfriendpage.qss");
    // QWidget 子类必须开启此属性，QSS 的 background 才会生效
    setAttribute(Qt::WA_StyledBackground);

    connect(ui->applyFriendList, &ApplyFriendList::SigShowSearch, this, &ApplyFriendPage::SigShowSearch);
    LoadApplyList();
    // 好友认证完成后刷新申请条目的状态
    connect(&FriendService::GetInstance(), &FriendService::SigFriendAuth, this, &ApplyFriendPage::SlotFriendAuth);
}

ApplyFriendPage::~ApplyFriendPage()
{
    delete ui;
}

void ApplyFriendPage::AddNewApply(std::shared_ptr<AddFriendApply> apply)
{
    if (!apply) {
        return;
    }
    // 头像直接使用服务端下发的 icon（Utils::ResolveIcon 统一解析并兜底）
    auto *apply_item = new ApplyFriendItem();
    auto apply_info =
        std::make_shared<ApplyInfo>(apply->uid_, apply->name_, apply->desc_, apply->icon_, apply->nick_, 0, 0);
    apply_item->SetInfo(apply_info);
    QListWidgetItem *item = new QListWidgetItem;
    item->setSizeHint(apply_item->sizeHint());
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    ui->applyFriendList->insertItem(0, item);
    ui->applyFriendList->setItemWidget(item, apply_item);
    apply_item->ShowAddBtn(true);
    unauthItems_[apply_info->uid_] = apply_item;
    connect(apply_item, &ApplyFriendItem::sigAuthFriend, this, &ApplyFriendPage::SlotAuthFriend);
}

void ApplyFriendPage::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ApplyFriendPage::LoadApplyList()
{
    auto apply_list = UserManager::GetInstance().GetApplyList();
    for (auto &[k, apply] : apply_list) {
        auto *apply_item = new ApplyFriendItem();
        apply->SetIcon(Utils::ResolveIcon(apply->icon_));
        apply_item->SetInfo(apply);
        QListWidgetItem *item = new QListWidgetItem;
        item->setSizeHint(apply_item->sizeHint());
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        ui->applyFriendList->insertItem(0, item);
        ui->applyFriendList->setItemWidget(item, apply_item);
        if (apply->status_) {
            apply_item->ShowAddBtn(false);
        } else {
            apply_item->ShowAddBtn(true);
            auto uid = apply_item->GetUid();
            unauthItems_[uid] = apply_item;
        }
        connect(apply_item, &ApplyFriendItem::sigAuthFriend, this, &ApplyFriendPage::SlotAuthFriend);
    }
}

void ApplyFriendPage::SlotFriendAuth(std::shared_ptr<FriendInfo> info)
{
    if (!info) {
        return;
    }
    auto find_iter = unauthItems_.find(info->uid_);
    if (find_iter == unauthItems_.end()) {
        return;
    }

    find_iter->second->ShowAddBtn(false);
    unauthItems_.erase(find_iter);
}

void ApplyFriendPage::SlotAuthFriend(std::shared_ptr<ApplyInfo> info)
{
    LOG_DEBUG() << "SlotAuthFriend";
    auto *authFriend = new ApplyFriendDialog(false, this);
    authFriend->SetApplyInfo(info);
    connect(authFriend, &ApplyFriendDialog::sigRejectFriend, this, &ApplyFriendPage::SlotRejectFriend);
    authFriend->show();
}

void ApplyFriendPage::SlotRejectFriend(std::shared_ptr<ApplyInfo> info)
{
    if (!info) {
        return;
    }
    auto find_iter = unauthItems_.find(info->uid_);
    if (find_iter == unauthItems_.end()) {
        return;
    }
    // 标记为已拒绝，并从未认证列表移除
    find_iter->second->ShowRejected();
    unauthItems_.erase(find_iter);
}
