#include "contactuserlist.h"
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QMovie>
#include <QTimer>
#include <QLabel>
#include "grouptipitem.h"
#include "contactuseritem.h"
#include "friendservice.h"
#include "userdata.h"
#include "usermanager.h"
#include "utils.h"
#include "log.h"
namespace {

// 条目身份数据挂在 QListWidgetItem 上（widget 为纯视图）：
// kItemTypeRole 存 ListItemType，kItemUidRole 存联系人 uid（仅联系人条目有效）
constexpr int kItemTypeRole = Qt::UserRole;
constexpr int kItemUidRole = Qt::UserRole + 1;

} // namespace
ContactUserList::ContactUserList(QWidget *parent) : QListWidget(parent)
{
    Q_UNUSED(parent);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 安装事件过滤器
    this->viewport()->installEventFilter(this);

    // 装配固定条目 + 加载第一页联系人
    InitList();
    AddContactList();
    // 连接点击的信号和槽
    connect(this, &QListWidget::itemClicked, this, &ContactUserList::SlotItemClicked);
    // 好友认证完成（对端同意或自己同意）后刷新通讯录
    connect(&FriendService::GetInstance(), &FriendService::SigFriendAuth, this, &ContactUserList::SlotFriendAuth);
}

void ContactUserList::ShowRedPoint(bool bshow /*= true*/)
{
    newFriendItem_->ShowRedPoint(bshow);
}

void ContactUserList::InitList()
{
    auto *groupTip = new GroupTipItem();
    QListWidgetItem *item = new QListWidgetItem;
    item->setSizeHint(groupTip->sizeHint());
    this->addItem(item);
    this->setItemWidget(item, groupTip);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);

    newFriendItem_ = new ContactUserItem();
    newFriendItem_->setObjectName("new_friend_item");
    newFriendItem_->SetInfo(tr("新的朋友"), ":/images/add_friend.png");

    QListWidgetItem *add_item = new QListWidgetItem;
    add_item->setSizeHint(newFriendItem_->sizeHint());
    // 身份由 item role 表达：这是"新的朋友"入口
    add_item->setData(kItemTypeRole, static_cast<int>(ListItemType::APPLY_FRIEND_ITEM));
    this->addItem(add_item);
    this->setItemWidget(add_item, newFriendItem_);

    // 默认设置新的朋友申请条目被选中
    this->setCurrentItem(add_item);

    // "自己"入口：显示当前登录用户资料（ResolveIcon 统一解析并兜底默认头像）
    auto self = UserManager::GetInstance().GetUserInfo();
    QString selfName = self && !self->name_.isEmpty() ? self->name_ : tr("我");
    QString selfIcon = self ? Utils::ResolveIcon(self->icon_) : Utils::ResolveIcon(QString());
    auto *selfItem = new ContactUserItem();
    selfItem->setObjectName("self_user_item");
    selfItem->SetInfo(selfName, selfIcon);

    QListWidgetItem *self_list_item = new QListWidgetItem;
    self_list_item->setSizeHint(selfItem->sizeHint());
    self_list_item->setData(kItemTypeRole, static_cast<int>(ListItemType::SELF_USER_ITEM));
    this->addItem(self_list_item);
    this->setItemWidget(self_list_item, selfItem);

    auto *groupCon = new GroupTipItem();
    groupCon->SetGroupTip(tr("联系人"));
    groupItem_ = new QListWidgetItem;
    groupItem_->setSizeHint(groupCon->sizeHint());
    this->addItem(groupItem_);
    this->setItemWidget(groupItem_, groupCon);
    groupItem_->setFlags(groupItem_->flags() & ~Qt::ItemIsSelectable);
}

void ContactUserList::AddContactList()
{
    auto list = UserManager::GetInstance().GetContactListPerPage();
    UserManager::GetInstance().UpdateContactLoadedCount();
    for (const auto &e : list) {
        if (addedUids_.contains(e->uid_)) {
            continue;
        }
        addedUids_.insert(e->uid_);
        auto *userItem = new ContactUserItem();
        userItem->SetInfo(e->name_, Utils::ResolveIcon(e->icon_));
        QListWidgetItem *item = new QListWidgetItem;
        item->setSizeHint(userItem->sizeHint());
        // 身份由 item role 表达：联系人条目 + uid
        item->setData(kItemTypeRole, static_cast<int>(ListItemType::CONTACT_USER_ITEM));
        item->setData(kItemUidRole, e->uid_);
        this->addItem(item);
        this->setItemWidget(item, userItem);
    }
}

void ContactUserList::LoadMoreUsers()
{
    LOG_DEBUG() << "=== Start Loading Contact Users ===";
    QString gifPath = ":/images/loading.gif";
    QMovie *movie = new QMovie(gifPath);

    if (!movie->isValid()) {
        LOG_WARN() << "Invalid GIF format";
        delete movie;
        m_loadingPending = false;
        return;
    }

    QLabel *loadingLabel = new QLabel();
    loadingLabel->setMovie(movie);
    loadingLabel->setFixedSize(QSize(250, 70));
    loadingLabel->setAlignment(Qt::AlignCenter);
    loadingLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    movie->setScaledSize(QSize(50, 50));
    QListWidgetItem *item = new QListWidgetItem(this);
    this->setItemWidget(item, loadingLabel);
    item->setSizeHint(QSize(250, 70));
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    movie->start();

    QTimer::singleShot(1000, this, [this, item, loadingLabel, movie]() {
        LOG_DEBUG() << "=== Finish Loading Contact Users, Cleaning up ===";
        AddContactList();
        int row = this->row(item);
        QListWidgetItem *takenItem = this->takeItem(row);
        if (takenItem) {
            delete takenItem;
        }
        if (loadingLabel) {
            delete loadingLabel;
        }
        movie->stop();
        movie->deleteLater();
        this->update();
        m_loadingPending = false;
    });
}

bool ContactUserList::eventFilter(QObject *watched, QEvent *event)
{
    // 检查事件是否是鼠标悬浮进入或离开
    if (watched == this->viewport()) {
        if (event->type() == QEvent::Enter) {
            // 鼠标悬浮，显示滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        } else if (event->type() == QEvent::Leave) {
            // 鼠标离开，隐藏滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    // 检查事件是否是鼠标滚轮事件
    if (watched == this->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent *wheelEvent = static_cast<QWheelEvent *>(event);
        int numDegrees = wheelEvent->angleDelta().y() / 8;
        int numSteps = numDegrees / 15; // 计算滚动步数

        // 设置滚动幅度
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - numSteps);

        // 检查是否滚动到底部
        QScrollBar *scrollBar = this->verticalScrollBar();
        int maxScrollValue = scrollBar->maximum();
        int currentValue = scrollBar->value();
        // int pageSize = 10; // 每页加载的联系人数量

        if (maxScrollValue - currentValue <= 0 && !m_loadingPending &&
            !UserManager::GetInstance().IsLoadContactFinish()) {
            // 滚动到底部，加载新的联系人
            m_loadingPending = true;
            LOG_DEBUG() << "load more contact user";
            LoadMoreUsers();
        }

        return true; // 停止事件传递
    }

    return QListWidget::eventFilter(watched, event);
}

void ContactUserList::SlotItemClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    // 身份数据从 item 的 role 读取；分组标题、loading 等未设置 role，直接忽略
    QVariant typeVar = item->data(kItemTypeRole);
    if (!typeVar.isValid()) {
        LOG_DEBUG() << "slot item clicked without type role, ignore";
        return;
    }

    auto itemType = static_cast<ListItemType>(typeVar.toInt());
    if (itemType == ListItemType::APPLY_FRIEND_ITEM) {
        // "新的朋友"入口：跳转到好友申请界面
        LOG_DEBUG() << "apply friend item clicked ";
        emit SigSwitchApplyFriendPage();
        return;
    }

    if (itemType == ListItemType::SELF_USER_ITEM) {
        // "自己"入口：跳转显示当前登录用户资料
        LOG_DEBUG() << "self user item clicked";
        emit SigSwitchSelfInfoPage();
        return;
    }

    if (itemType == ListItemType::CONTACT_USER_ITEM) {
        // 联系人条目：携带 uid 跳转到好友资料页
        int uid = item->data(kItemUidRole).toInt();
        LOG_DEBUG() << "contact user item clicked, uid is" << uid;
        emit SigSwitchFriendInfoPage(uid);
        return;
    }
}

void ContactUserList::SlotFriendAuth(std::shared_ptr<FriendInfo> info)
{
    if (!info) {
        return;
    }
    // 本地去重：已在通讯录列表中则跳过（不依赖 UserManager 状态，避免信号顺序导致漏加）
    if (addedUids_.contains(info->uid_)) {
        return;
    }
    addedUids_.insert(info->uid_);

    auto *userWidget = new ContactUserItem();
    userWidget->SetInfo(info->name_, Utils::ResolveIcon(info->icon_));
    QListWidgetItem *item = new QListWidgetItem;
    item->setSizeHint(userWidget->sizeHint());
    // 身份由 item role 表达：联系人条目 + uid
    item->setData(kItemTypeRole, static_cast<int>(ListItemType::CONTACT_USER_ITEM));
    item->setData(kItemUidRole, info->uid_);

    // 获取 groupitem 的索引，在其后插入新项
    int index = this->row(groupItem_);
    this->insertItem(index + 1, item);
    this->setItemWidget(item, userWidget);
}
