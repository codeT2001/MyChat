#include "chatuserlist.h"
#include "log.h"
#include "userwidget.h"
#include "usermanager.h"
#include "userdata.h"
#include "utils.h"
#include <QScrollBar>
#include <QStyle>
#include <QHBoxLayout>

namespace {
// 条目卡片四周留白：卡片不直接铺满列表行，露出列表底色形成圆角卡片效果
constexpr int kCardMarginH = 6;
constexpr int kCardMarginV = 3;

// 包一层带留白的容器，UserWidget 圆角卡片居中其中
QWidget *WrapItemWidget(QWidget *inner)
{
    auto *wrap = new QWidget();
    auto *lay = new QHBoxLayout(wrap);
    lay->setContentsMargins(kCardMarginH, kCardMarginV, kCardMarginH, kCardMarginV);
    lay->addWidget(inner);
    return wrap;
}
} // namespace
#include <QDebug>
#include <QEvent>
#include <QMovie>
#include <QTimer>
#include <QLabel>

ChatUserList::ChatUserList(QWidget *parent) : QListWidget(parent)
{
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setUniformItemSizes(true); // 帮助 Qt 准确计算内容高度

    this->viewport()->installEventFilter(this);

    // 点击会话条目：从 item 的 UserRole 反查 uid 并通知外部
    connect(this, &QListWidget::itemClicked, this, &ChatUserList::SlotItemClicked);

    connect(this->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar *bar = this->verticalScrollBar();
        int maxValue = bar->maximum();

        // 接近底部时触发加载，但加防抖防止滚动条停在底部时连续触发
        if (maxValue - value <= 50 && !m_loadingPending && !UserManager::GetInstance().IsLoadChatFinish()) {
            m_loadingPending = true;
            LOG_DEBUG() << "Load more chat user triggered by scroll";
            LoadMoreUsers();
        }
    });
}

void ChatUserList::LoadInitialUsers()
{
    AddUserList();
}

void ChatUserList::LoadMoreUsers()
{
    LOG_DEBUG() << "=== Start Loading Users ===";
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

    item->setSizeHint(QSize(250, 70 + 2 * kCardMarginV)); // 与用户条目等高，配合 setUniformItemSizes
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    movie->start();

    LOG_DEBUG() << "Loading item added. Frame count:" << movie->frameCount();

    QTimer::singleShot(1000, this, [this, item, loadingLabel, movie]() {
        LOG_DEBUG() << "=== Finish Loading, Cleaning up ===";
        AddUserList();
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

void ChatUserList::AddUserList()
{
    auto list = UserManager::GetInstance().GetChatListPerPage();
    UserManager::GetInstance().UpdateChatLoadedCount();
    for (const auto &e : list) {
        if (chatItemsAdded_.contains(e->uid_)) {
            continue;
        }

        auto *userWidget = new UserWidget();
        userWidget->SetInfo(e->name_, Utils::ResolveIcon(e->icon_), e->last_msg_);
        QListWidgetItem *item = new QListWidgetItem;
        item->setSizeHint(QSize(250, userWidget->sizeHint().height() + 2 * kCardMarginV));
        item->setData(Qt::UserRole, e->uid_); // uid 挂在 item 上，点击时反查
        this->addItem(item);
        this->setItemWidget(item, WrapItemWidget(userWidget));
        chatItemsAdded_.insert(e->uid_, item);
    }
    this->update();
}

void ChatUserList::InsertUserItem(std::shared_ptr<FriendInfo> info)
{
    if (!info || chatItemsAdded_.contains(info->uid_)) {
        return;
    }

    auto *userWidget = new UserWidget();
    userWidget->SetInfo(info->name_, Utils::ResolveIcon(info->icon_), info->last_msg_);
    QListWidgetItem *item = new QListWidgetItem;
    item->setSizeHint(QSize(250, userWidget->sizeHint().height() + 2 * kCardMarginV));
    item->setData(Qt::UserRole, info->uid_); // uid 挂在 item 上，点击时反查
    this->insertItem(0, item);
    this->setItemWidget(item, WrapItemWidget(userWidget));
    chatItemsAdded_.insert(info->uid_, item);
}

UserWidget *ChatUserList::FindItemWidget(QListWidgetItem *item) const
{
    if (!item) {
        return nullptr;
    }
    // 条目 widget 外包了一层留白容器，兼容直接挂载和包裹挂载两种情况
    QWidget *w = this->itemWidget(item);
    if (!w) {
        return nullptr;
    }
    if (auto *inner = qobject_cast<UserWidget *>(w)) {
        return inner;
    }
    return w->findChild<UserWidget *>();
}

void ChatUserList::SetItemRedPoint(int uid, bool show)
{
    if (!chatItemsAdded_.contains(uid)) {
        return;
    }
    auto *widget = FindItemWidget(chatItemsAdded_.value(uid));
    if (widget) {
        widget->SetShowRedPoint(show);
    }
}

void ChatUserList::ClearItemRedPoint(int uid)
{
    SetItemRedPoint(uid, false);
}

void ChatUserList::SlotItemClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    // loading 动画 item 没有设置 UserRole，直接忽略
    QVariant uidVar = item->data(Qt::UserRole);
    if (!uidVar.isValid()) {
        return;
    }
    int uid = uidVar.toInt();
    ClearItemRedPoint(uid);
    UpdateSelection(item);
    emit SigChatItemClicked(uid);
}

void ChatUserList::UpdateSelection(QListWidgetItem *clicked)
{
    // 条目 widget 自身不透明且带圆角，会盖住 ::item 的方形选中背景，
    // 因此选中态改由 widget 的 selected 动态属性驱动 QSS
    for (auto it = chatItemsAdded_.cbegin(); it != chatItemsAdded_.cend(); ++it) {
        auto *widget = FindItemWidget(it.value());
        if (!widget) {
            continue;
        }
        bool selected = (it.value() == clicked);
        if (widget->property("selected").toBool() == selected) {
            continue;
        }
        widget->setProperty("selected", selected);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
}

bool ChatUserList::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this->viewport()) {
        if (event->type() == QEvent::Enter) {
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        } else if (event->type() == QEvent::Leave) {
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }
    return QListWidget::eventFilter(watched, event);
}
