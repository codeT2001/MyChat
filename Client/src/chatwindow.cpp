#include "chatwindow.h"
#include "ui_chatwindow.h"
#include "friendservice.h"
#include "chatservice.h"
#include "utils.h"
#include "userdata.h"
#include "usermanager.h"
#include "log.h"
#include <QAction>
#include <QMouseEvent>
ChatWindow::ChatWindow(QWidget *parent)
    : QWidget(parent), ui(new Ui::ChatWindow), mode_(ChatMode::CHATS_MODE), state_(ListType::CHATS_LIST)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/chatwindow.qss");
    // 通讯录模式默认显示新的朋友页
    lastContactPage_ = ui->applyFriendPage;
    this->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    QAction *searchAction = new QAction(ui->searchEdit);
    searchAction->setIcon(QIcon(":/images/search.png"));
    ui->searchEdit->addAction(searchAction, QLineEdit::LeadingPosition);
    ui->searchList->SetSearchEdit(ui->searchEdit);
    connect(ui->sideChatBtn, &BadgeButton::clicked, this, &ChatWindow::OnSideChatBtnClicked);
    connect(ui->sideContBtn, &BadgeButton::clicked, this, &ChatWindow::OnSideContBtnClicked);
    connect(ui->searchEdit, &QLineEdit::textChanged, this, &ChatWindow::OnSearchEditTextChanged);
    connect(&FriendService::GetInstance(), &FriendService::SigFriendApply, this, &ChatWindow::SlotFriendApply);
    connect(&FriendService::GetInstance(), &FriendService::SigFriendAuth, this, &ChatWindow::SlotFriendAuth);
    connect(&FriendService::GetInstance(), &FriendService::SigFriendRejected, this,
            [](std::shared_ptr<AddFriendApply> info) {
                Q_UNUSED(info);
                LOG_INFO() << "your friend request has been rejected";
            });
    // 通讯录列表的页面切换
    connect(ui->contactsList, &ContactUserList::SigSwitchApplyFriendPage, this, [this]() {
        lastContactPage_ = ui->applyFriendPage;
        ui->chatDataStackWidget->setCurrentWidget(ui->applyFriendPage);
    });
    connect(ui->contactsList, &ContactUserList::SigSwitchFriendInfoPage, this,
            &ChatWindow::SlotSwitchFriendInfoPage);
    connect(ui->contactsList, &ContactUserList::SigSwitchSelfInfoPage, this,
            &ChatWindow::SlotSwitchSelfInfoPage);
    // 聊天列表点击会话：切换到对应好友的聊天页
    connect(ui->usersList, &ChatUserList::SigChatItemClicked, this, &ChatWindow::SlotChatItemClicked);
    // 搜索命中已有好友：跳转到与该好友的聊天页
    connect(ui->searchList, &SearchList::SigJumpToChat, this, &ChatWindow::SlotChatItemClicked);
    // 收到对端文本消息：刷新当前聊天页视图
    connect(&ChatService::GetInstance(), &ChatService::SigTextChatMsgReceived, this,
            &ChatWindow::SlotTextChatMsgReceived);
    ui->usersList->LoadInitialUsers();
    AddBadgeButtonToGroup();
    this->installEventFilter(this);
    connect(ui->stackedWidget, &QStackedWidget::currentChanged, this, [this](int index) {
        Q_UNUSED(index);
        QWidget *currentWidget = ui->stackedWidget->currentWidget();
        if (currentWidget) {
#ifdef QT_DEBUG
            LOG_DEBUG() << "[ChatWindow] page switched to:" << currentWidget->metaObject()->className();
#endif
        }
    });
}

ChatWindow::~ChatWindow()
{
    delete ui;
}

bool ChatWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        HandleGlobalMousePress(mouseEvent);
    }
    return QWidget::eventFilter(watched, event);
}

void ChatWindow::OnSideChatBtnClicked()
{
    ui->stackedWidget->setCurrentWidget(ui->chatsPage);
    // 有活跃会话则恢复聊天页，否则显示空白页
    if (ui->chatPage->GetCurrentUid() >= 0) {
        ui->chatDataStackWidget->setCurrentWidget(ui->chatPage);
    } else {
        ui->chatDataStackWidget->setCurrentWidget(ui->chatDataEmptyPage);
    }
    ui->sideChatBtn->setShowBadge(false);
    mode_ = ChatMode::CHATS_MODE;
    state_ = ListType::CHATS_LIST;
}

void ChatWindow::OnSideContBtnClicked()
{
    ui->stackedWidget->setCurrentWidget(ui->contactsPage);
    // 恢复通讯录模式下上次显示的页面
    ui->chatDataStackWidget->setCurrentWidget(lastContactPage_);
    mode_ = ChatMode::CONTACTS_NODE;
    state_ = ListType::CONTACTS_LIST;
}

void ChatWindow::OnSearchEditTextChanged(const QString &text)
{
    // state_ = ListType::SEARCH_LIST;
    if (!text.isEmpty()) {
        ShowSearchList(true);
    }
}

void ChatWindow::SlotFriendApply(std::shared_ptr<AddFriendApply> info)
{
    if (!info || UserManager::GetInstance().AlreadyApply(info->uid_)) {
        return;
    }
    UserManager::GetInstance().AddApply(std::make_shared<ApplyInfo>(info));
    ui->sideContBtn->setShowBadge(true);
    ui->contactsList->ShowRedPoint(true);
    ui->applyFriendPage->AddNewApply(info);
}

void ChatWindow::SlotFriendAuth(std::shared_ptr<FriendInfo> info)
{
    if (!info) {
        return;
    }
    LOG_DEBUG() << "receive SlotFriendAuth uid is " << info->uid_ << " name is " << info->name_ << " nick is "
                << info->nick_;
    // 新好友插入聊天列表（去重与渲染由 ChatUserList 自治）
    ui->usersList->InsertUserItem(info);
}

void ChatWindow::SlotSwitchFriendInfoPage(int uid)
{
    auto info = UserManager::GetInstance().GetFriendById(uid);
    if (!info) {
        LOG_WARN() << "switch friend info page failed, friend not found, uid =" << uid;
        return;
    }
    ui->infoPage->SetInfo(info);
    lastContactPage_ = ui->infoPage;
    ui->chatDataStackWidget->setCurrentWidget(ui->infoPage);
}

void ChatWindow::SlotSwitchSelfInfoPage()
{
    auto self = UserManager::GetInstance().GetUserInfo();
    if (!self) {
        LOG_WARN() << "switch self info page failed, user info is null";
        return;
    }
    ui->infoPage->SetSelfInfo(self);
    lastContactPage_ = ui->infoPage;
    ui->chatDataStackWidget->setCurrentWidget(ui->infoPage);
}

void ChatWindow::ShowSearchList(bool show)
{
    if (show) {
        mode_ = ChatMode::SEARCH_MODE;
        ui->stackedWidget->setCurrentWidget(ui->searchPage);
        return;
    }
    ui->searchEdit->clear();
    ui->searchEdit->clearFocus();
    if (state_ == ListType::CHATS_LIST) {
        ui->stackedWidget->setCurrentWidget(ui->chatsPage);
        mode_ = ChatMode::CHATS_MODE;
    }
    if (state_ == ListType::CONTACTS_LIST) {
        ui->stackedWidget->setCurrentWidget(ui->contactsPage);
        mode_ = ChatMode::CONTACTS_NODE;
    }
}

void ChatWindow::HandleGlobalMousePress(QMouseEvent *event)
{
    if (mode_ != ChatMode::SEARCH_MODE) {
        return;
    }
    QPoint posInSearchList = ui->searchList->mapFromGlobal(event->globalPosition().toPoint());
    if (!ui->searchList->rect().contains(posInSearchList)) {
        ui->searchList->clear();
        ShowSearchList(false);
    }
}

void ChatWindow::AddBadgeButtonToGroup()
{
    buttonGroup_ = new QButtonGroup(this);
    buttonGroup_->setExclusive(true);
    buttonGroup_->addButton(ui->sideChatBtn);
    buttonGroup_->addButton(ui->sideContBtn);
    ui->sideChatBtn->setChecked(true);
}

void ChatWindow::SlotChatItemClicked(int uid)
{
    auto info = UserManager::GetInstance().GetFriendById(uid);
    if (!info) {
        LOG_WARN() << "chat item clicked, friend not found, uid =" << uid;
        return;
    }
    // 若从搜索页跳转，先回到聊天列表页并清空搜索框
    if (mode_ == ChatMode::SEARCH_MODE) {
        ui->searchEdit->clear();
        ui->stackedWidget->setCurrentWidget(ui->chatsPage);
        mode_ = ChatMode::CHATS_MODE;
        state_ = ListType::CHATS_LIST;
    }
    ui->chatPage->SetChatFriend(info);
    // 确保右侧切到聊天页
    ui->chatDataStackWidget->setCurrentWidget(ui->chatPage);
}

void ChatWindow::SlotTextChatMsgReceived(int fromUid, std::vector<std::shared_ptr<TextChatData>> msgs)
{
    // 仅当当前正与该好友聊天时，才把新消息追加到视图
    if (ui->chatPage->GetCurrentUid() != fromUid) {
        // 非当前会话：会话列表条目红点 + 侧边栏红点
        ui->usersList->SetItemRedPoint(fromUid, true);
        ui->sideChatBtn->setShowBadge(true);
        return;
    }
    auto friendInfo = UserManager::GetInstance().GetFriendById(fromUid);
    if (!friendInfo) {
        LOG_WARN() << "received chat msg but friend not found, uid =" << fromUid;
        return;
    }
    QString name = friendInfo->name_.isEmpty() ? friendInfo->nick_ : friendInfo->name_;
    QString icon = friendInfo->icon_.isEmpty() ? ":/images/head_1.jpg" : friendInfo->icon_;
    for (const auto &msg : msgs) {
        if (!msg) {
            continue;
        }
        ui->chatPage->AppendPeerMessage(name, icon, msg->msg_content_);
    }
}
