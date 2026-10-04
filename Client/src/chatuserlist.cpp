#include "chatuserlist.h"
#include "log.h"
#include "userwidget.h"
#include "usermanager.h"
#include "userdata.h"
#include <QScrollBar>
#include <QDebug>
#include <QEvent>
#include <QMovie>
#include <QTimer>
#include <QLabel>
#include <QRandomGenerator>

namespace {
const std::vector<QString> heads = {":/images/head_1.jpg", ":/images/head_2.jpg", ":/images/head_3.jpg",
                                    ":/images/head_4.jpg", ":/images/head_5.jpg"};
}

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

    item->setSizeHint(QSize(250, 70));
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

        int randomValue = QRandomGenerator::global()->bounded(100);
        int head_i = randomValue % heads.size();

        auto *userWidget = new UserWidget();
        userWidget->SetInfo(e->name_, heads[head_i], e->last_msg_);
        QListWidgetItem *item = new QListWidgetItem;
        item->setSizeHint(userWidget->sizeHint());
        item->setData(Qt::UserRole, e->uid_); // uid 挂在 item 上，点击时反查
        this->addItem(item);
        this->setItemWidget(item, userWidget);
        chatItemsAdded_.insert(e->uid_, item);
    }
    this->update();
}

void ChatUserList::InsertUserItem(std::shared_ptr<FriendInfo> info)
{
    if (!info || chatItemsAdded_.contains(info->uid_)) {
        return;
    }

    int randomValue = QRandomGenerator::global()->bounded(100);
    int head_i = randomValue % heads.size();

    auto *userWidget = new UserWidget();
    userWidget->SetInfo(info->name_, heads[head_i], info->last_msg_);
    QListWidgetItem *item = new QListWidgetItem;
    item->setSizeHint(userWidget->sizeHint());
    item->setData(Qt::UserRole, info->uid_); // uid 挂在 item 上，点击时反查
    this->insertItem(0, item);
    this->setItemWidget(item, userWidget);
    chatItemsAdded_.insert(info->uid_, item);
}

void ChatUserList::SetItemRedPoint(int uid, bool show)
{
    if (!chatItemsAdded_.contains(uid)) {
        return;
    }
    auto *item = chatItemsAdded_.value(uid);
    if (!item) {
        return;
    }
    auto *widget = qobject_cast<UserWidget *>(this->itemWidget(item));
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
    emit SigChatItemClicked(uid);
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
