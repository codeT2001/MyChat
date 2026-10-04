#include "mainpanel.h"
#include "ui_mainpanel.h"
#include "friendservice.h"
#include "chatservice.h"
#include "utils.h"
#include "domainmodels.h"
#include "usermanager.h"
#include "logger.h"
#include <QAction>
#include <QMouseEvent>
MainPanel::MainPanel(QWidget *parent) : QWidget(parent), ui(new Ui::MainPanel), pageMode_(PageMode::ChatsPage)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/mainpanel.qss");
    // 通讯录模式默认显示新的朋友页
    lastContactPage_ = ui->applyFriendPage;
    this->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    QAction *searchAction = new QAction(ui->searchEdit);
    searchAction->setIcon(QIcon(":/images/search.png"));
    ui->searchEdit->addAction(searchAction, QLineEdit::LeadingPosition);
    ui->searchList->SetSearchEdit(ui->searchEdit);
    connect(ui->sideChatBtn, &BadgeButton::clicked, this, &MainPanel::OnSideChatBtnClicked);
    connect(ui->sideContBtn, &BadgeButton::clicked, this, &MainPanel::OnSideContBtnClicked);
    connect(ui->searchEdit, &QLineEdit::textChanged, this, &MainPanel::OnSearchEditTextChanged);
    connect(&FriendService::GetInstance(), &FriendService::SigFriendApply, this, &MainPanel::OnFriendApply);
    connect(&FriendService::GetInstance(), &FriendService::SigFriendAccepted, this, &MainPanel::OnFriendAccepted);
    connect(&FriendService::GetInstance(), &FriendService::SigFriendRejected, this, []() {
        LOG_INFO() << "your friend request has been rejected";
    });
    // 通讯录列表的页面切换
    connect(ui->contactsList, &ContactUserList::SigSwitchApplyFriendPage, this, [this]() {
        lastContactPage_ = ui->applyFriendPage;
        ui->chatDataStackWidget->setCurrentWidget(ui->applyFriendPage);
    });
    connect(ui->contactsList, &ContactUserList::SigSwitchFriendInfoPage, this,
            &MainPanel::OnSwitchFriendInfoPage);
    connect(ui->contactsList, &ContactUserList::SigSwitchSelfInfoPage, this,
            &MainPanel::OnSwitchSelfInfoPage);
    // 聊天列表点击会话：切换到对应好友的聊天页
    connect(ui->sessionList, &ChatSessionList::SigChatItemClicked, this, &MainPanel::OnChatItemClicked);
    // 搜索命中已有好友：跳转到与该好友的聊天页
    connect(ui->searchList, &SearchList::SigJumpToChat, this, &MainPanel::OnChatItemClicked);
    // 收到对端文本消息：刷新当前聊天页视图
    connect(&ChatService::GetInstance(), &ChatService::SigTextChatMsgReceived, this,
            &MainPanel::OnTextChatMsgReceived);
    ui->sessionList->LoadInitialSessions();
    SetupSideButtonGroup();
    this->installEventFilter(this);
    connect(ui->stackedWidget, &QStackedWidget::currentChanged, this, [this](int index) {
        Q_UNUSED(index);
        QWidget *currentWidget = ui->stackedWidget->currentWidget();
        if (currentWidget) {
#ifdef QT_DEBUG
            LOG_DEBUG() << "[MainPanel] page switched to:" << currentWidget->metaObject()->className();
#endif
        }
    });
}

MainPanel::~MainPanel()
{
    delete ui;
}

bool MainPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        HandleGlobalMousePress(mouseEvent);
    }
    return QWidget::eventFilter(watched, event);
}

void MainPanel::OnSideChatBtnClicked()
{
    ui->stackedWidget->setCurrentWidget(ui->chatsPage);
    // 有活跃会话则恢复聊天页，否则显示空白页
    if (ui->chatPage->GetCurrentUid() >= 0) {
        ui->chatDataStackWidget->setCurrentWidget(ui->chatPage);
    } else {
        ui->chatDataStackWidget->setCurrentWidget(ui->chatDataEmptyPage);
    }
    ui->sideChatBtn->SetShowBadge(false);
    pageMode_ = PageMode::ChatsPage;
}

void MainPanel::OnSideContBtnClicked()
{
    ui->stackedWidget->setCurrentWidget(ui->contactsPage);
    // 恢复通讯录模式下上次显示的页面
    ui->chatDataStackWidget->setCurrentWidget(lastContactPage_);
    pageMode_ = PageMode::ContactsPage;
}

void MainPanel::OnSearchEditTextChanged(const QString &text)
{
    if (!text.isEmpty()) {
        ShowSearchList(true);
    }
}

void MainPanel::OnFriendApply(std::shared_ptr<AddFriendApply> info)
{
    if (!info || UserManager::GetInstance().AlreadyApply(info->uid_)) {
        return;
    }
    UserManager::GetInstance().AddApply(std::make_shared<ApplyInfo>(info));
    ui->sideContBtn->SetShowBadge(true);
    ui->contactsList->ShowNewFriendBadge(true);
    ui->applyFriendPage->AddNewApply(info);
}

void MainPanel::OnFriendAccepted(std::shared_ptr<FriendInfo> info)
{
    if (!info) {
        return;
    }
    LOG_DEBUG() << "receive OnFriendAccepted uid is " << info->uid_ << " name is " << info->name_ << " nick is "
                << info->nick_;
    // 新好友插入聊天列表（去重与渲染由 ChatSessionList 自治）
    ui->sessionList->InsertSessionItem(info);
}

void MainPanel::OnSwitchFriendInfoPage(int uid)
{
    auto info = UserManager::GetInstance().GetFriendById(uid);
    if (!info) {
        LOG_WARN() << "switch friend info page failed, friend not found, uid =" << uid;
        return;
    }
    ui->infoPage->SetFriendInfo(info);
    lastContactPage_ = ui->infoPage;
    ui->chatDataStackWidget->setCurrentWidget(ui->infoPage);
}

void MainPanel::OnSwitchSelfInfoPage()
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

void MainPanel::ShowSearchList(bool show)
{
    if (show) {
        pageMode_ = PageMode::SearchPage;
        ui->stackedWidget->setCurrentWidget(ui->searchPage);
        return;
    }
    ui->searchEdit->clear();
    ui->searchEdit->clearFocus();
    // 退出搜索：侧边栏选中哪个按钮就回到哪个页面（按钮组互斥，切页操作同源）
    if (ui->sideChatBtn->isChecked()) {
        ui->stackedWidget->setCurrentWidget(ui->chatsPage);
        pageMode_ = PageMode::ChatsPage;
    } else {
        ui->stackedWidget->setCurrentWidget(ui->contactsPage);
        pageMode_ = PageMode::ContactsPage;
    }
}

void MainPanel::HandleGlobalMousePress(QMouseEvent *event)
{
    if (pageMode_ != PageMode::SearchPage) {
        return;
    }
    QPoint posInSearchList = ui->searchList->mapFromGlobal(event->globalPosition().toPoint());
    if (!ui->searchList->rect().contains(posInSearchList)) {
        ui->searchList->clear();
        ShowSearchList(false);
    }
}

void MainPanel::SetupSideButtonGroup()
{
    buttonGroup_ = new QButtonGroup(this);
    buttonGroup_->setExclusive(true);
    buttonGroup_->addButton(ui->sideChatBtn);
    buttonGroup_->addButton(ui->sideContBtn);
    ui->sideChatBtn->setChecked(true);
}

void MainPanel::OnChatItemClicked(int uid)
{
    auto info = UserManager::GetInstance().GetFriendById(uid);
    if (!info) {
        LOG_WARN() << "chat item clicked, friend not found, uid =" << uid;
        return;
    }
    // 若从搜索页跳转，先回到聊天列表页并清空搜索框
    if (pageMode_ == PageMode::SearchPage) {
        ui->searchEdit->clear();
        ui->stackedWidget->setCurrentWidget(ui->chatsPage);
        pageMode_ = PageMode::ChatsPage;
    }
    ui->chatPage->SetChatFriend(info);
    // 确保右侧切到聊天页
    ui->chatDataStackWidget->setCurrentWidget(ui->chatPage);
}

void MainPanel::OnTextChatMsgReceived(int fromUid, std::vector<std::shared_ptr<TextChatData>> msgs)
{
    // 仅当当前正与该好友聊天时，才把新消息追加到视图
    if (ui->chatPage->GetCurrentUid() != fromUid) {
        // 非当前会话：会话列表条目红点 + 侧边栏红点
        ui->sessionList->SetItemRedPoint(fromUid, true);
        ui->sideChatBtn->SetShowBadge(true);
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
