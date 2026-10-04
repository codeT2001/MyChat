#include "searchlist.h"
#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <friendservice.h>
#include <QLineEdit>
#include "domainmodels.h"
#include "usermanager.h"
#include "searchentryitem.h"
#include "searchresultdialog.h"
#include "loadingdialog.h"
#include "logger.h"
SearchList::SearchList(QWidget *parent)
    : QListWidget(parent), searchResultDialog_(nullptr), searchEdit_(nullptr), searchPending_(false)
{
    Q_UNUSED(parent);
    loadingDialog_ = new LoadingDialog(this);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 安装事件过滤器
    this->viewport()->installEventFilter(this);
    // 连接点击的信号和槽
    connect(this, &QListWidget::itemClicked, this, &SearchList::OnItemClicked);
    // 添加条目
    //  AddSearchEntryItem();
    // 连接搜索条目
    connect(&FriendService::GetInstance(), &FriendService::SigUserSearch, this, &SearchList::OnUserSearch);
}

void SearchList::SetSearchEdit(QWidget *w)
{
    searchEdit_ = w;
}

bool SearchList::eventFilter(QObject *watched, QEvent *event)
{
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

        return true; // 停止事件传递
    }

    return QListWidget::eventFilter(watched, event);
}

void SearchList::OnItemClicked(QListWidgetItem *item)
{
    QWidget *widget = this->itemWidget(item);
    if (!widget) {
        LOG_DEBUG() << "slot item clicked widget is nullptr";
        return;
    }
    ListItemBase *customItem = qobject_cast<ListItemBase *>(widget);
    if (!customItem) {
        LOG_DEBUG() << "slot item clicked widget is nullptr";
        return;
    }

    auto itemType = customItem->GetItemType();
    if (itemType == ListItemType::INVALID_ITEM) {
        LOG_DEBUG() << "slot invalid item clicked ";
        return;
    }

    if (itemType == ListItemType::SEARCH_ENTRY_ITEM) {
        if (searchPending_ || (!searchEdit_)) {
            return;
        }
        SetSearchPending(true);
        if (QLineEdit *edit = qobject_cast<QLineEdit *>(searchEdit_)) {
            // 转换成功，安全使用 edit
            QString text = edit->text();
            bool ok;
            text.toInt(&ok);
            if (ok) {
                FriendService::GetInstance().SearchUser(text);
            } else {
                LOG_WARN() << "invalid uid";
            }
        } else {
            // 转换失败，searchEdit_ 不是 QLineEdit 类型
            LOG_WARN() << "searchEdit_ is not a QLineEdit!";
        }
        return;
    }
}

void SearchList::CloseSearchResultDialog()
{
    if (searchResultDialog_) {
        searchResultDialog_->hide();
        searchResultDialog_->deleteLater();
        searchResultDialog_ = nullptr;
    }
}
void SearchList::OnUserSearch(std::shared_ptr<SearchInfo> info)
{
    SetSearchPending(false);
    if (!info) {
        searchResultDialog_ = new SearchResultDialog(false, this);
        searchResultDialog_->show();
        return;
    }
    if (info->uid_ == UserManager::GetInstance().GetUid()) {
        return;
    }
    if (UserManager::GetInstance().CheckFriendById(info->uid_)) {
        // 已是好友，跳转到与该好友的聊天页
        emit SigJumpToChat(info->uid_);
        return;
    }
    searchResultDialog_ = new SearchResultDialog(true, this);
    searchResultDialog_->SetSearchInfo(info);
    searchResultDialog_->show();
}

void SearchList::SetSearchPending(bool pending)
{
    if (pending) {
        loadingDialog_->Start();
        loadingDialog_->show();
    } else {
        loadingDialog_->hide();
        loadingDialog_->Stop();
    }
    searchPending_ = pending;
}

void SearchList::AddSearchEntryItem()
{
    // auto *invalidItem = new QWidget();
    // QListWidgetItem *itemTmp = new QListWidgetItem;
    // itemTmp->setSizeHint(QSize(250,10));
    // this->addItem(itemTmp);
    // invalidItem->setObjectName("invalidItem");
    // this->setItemWidget(itemTmp, invalidItem);
    // itemTmp->setFlags(itemTmp->flags() & ~Qt::ItemIsSelectable);

    LOG_DEBUG() << "SearchList::AddSearchEntryItem()";
    auto *entryItem = new SearchEntryItem(this);
    QListWidgetItem *item = new QListWidgetItem;
    // qDebug()<<"chat_user_wid sizeHint is " << chat_user_wid->sizeHint();
    item->setSizeHint(entryItem->sizeHint());
    this->addItem(item);
    this->setItemWidget(item, entryItem);
}

void SearchList::showEvent(QShowEvent *event)
{
    // 1. 打印基础信息
    LOG_DEBUG() << ">>> SearchList 显示出来了！";
    LOG_DEBUG() << ">>> 当前 Item 数量:" << this->count();

    // 2. 检查是否还包含那个入口条目
    // 如果这里 count() 是 0，说明在切换过程中列表被清空了，或者根本没初始化
    if (this->count() == 0) {
        LOG_DEBUG() << ">>> 警告：列表是空的！可能需要重新 AddSearchEntryItem()";
        AddSearchEntryItem(); // 可以在这里补救
    }

    // 3. 调用父类函数（很重要，不要漏掉）
    QWidget::showEvent(event);
}
