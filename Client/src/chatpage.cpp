#include "chatpage.h"
#include "ui_chatpage.h"
#include "utils.h"
#include "log.h"
#include "userdata.h"
#include "usermanager.h"
#include "chatservice.h"
#include <QPaintEvent>
#include <QStyleOption>
#include <QPainter>
#include <QPixmap>
#include <QUuid>
#include <QJsonObject>
#include <QJsonArray>
#include "chatitembase.h"
#include "textbubble.h"
#include "picturebubble.h"

namespace {
constexpr const char *kDefaultHead = ":/images/head_1.jpg";
constexpr int kMaxTextBatchSize = 1024; // 单批文本消息总长度上限
constexpr int kMaxSingleMsgLen = 1024;  // 单条消息内容长度上限

// 加载头像，路径为空或加载失败时回退默认头像
QPixmap LoadIconOrDefault(const QString &icon)
{
    QPixmap p(icon);
    if (p.isNull()) {
        p.load(kDefaultHead);
    }
    return p;
}
} // namespace

ChatPage::ChatPage(QWidget *parent) : QWidget(parent), ui(new Ui::ChatPage)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/chatpage.qss");
    connect(ui->sendMsgBtn, &QPushButton::clicked, this, &ChatPage::OnSendMsgBtnClicked);
    connect(ui->chatEdit, &MessageTextEdit::Send, this, &ChatPage::OnSendMsgBtnClicked);
}

ChatPage::~ChatPage()
{
    delete ui;
}

void ChatPage::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ChatPage::SetChatFriend(std::shared_ptr<FriendInfo> info)
{
    if (!info) {
        LOG_WARN() << "SetChatFriend failed, friend info is null";
        return;
    }
    // 已是当前会话则无需重渲染
    if (currentUid_ == info->uid_) {
        return;
    }
    currentUid_ = info->uid_;

    // 1. 顶部标题：优先显示备注名 name_，回退昵称 nick_
    QString name = info->name_.isEmpty() ? info->nick_ : info->name_;
    ui->headerLabel->setText(name);

    // 2. 清空上一会话的消息渲染
    ui->chatListWidget->Clear();

    // 3. 渲染该好友的历史消息
    int selfUid = UserManager::GetInstance().GetUid();
    for (const auto &msg : info->chat_msgs_) {
        if (!msg) {
            continue;
        }
        bool isSelf = (msg->from_uid_ == selfUid);
        QString senderName = isSelf ? UserManager::GetInstance().GetName() : name;
        // icon 为空/无效均由 AppendMessage 内部回退默认头像
        QString senderIcon = isSelf ? UserManager::GetInstance().GetIcon() : info->icon_;
        // 历史消息当前统一按文本渲染（TextChatData 暂未区分消息类型）
        AppendMessage(isSelf, senderName, senderIcon, msg->msg_content_, "text");
    }
}

void ChatPage::AppendMessage(bool isSelf,
                             const QString &name,
                             const QString &icon,
                             const QString &content,
                             const QString &type)
{
    ChatItemBase *item = new ChatItemBase(isSelf);
    item->SetUserName(name);
    item->SetUserIcon(LoadIconOrDefault(icon));
    QWidget *bubble = nullptr;
    if (type == "text") {
        bubble = new TextBubble(content, isSelf);
    } else if (type == "image") {
        bubble = new PictureBubble(QPixmap(content), isSelf);
    } else if (type == "file") {
        // 文件气泡暂未实现，跳过
        LOG_WARN() << "file bubble not implemented, skip message";
        delete item;
        return;
    }
    if (bubble != nullptr) {
        item->SetWidget(bubble);
        ui->chatListWidget->AppendChatItem(item);
    }
}

void ChatPage::AppendPeerMessage(const QString &name, const QString &icon, const QString &content)
{
    AppendMessage(false, name, icon, content, "text");
}

void ChatPage::OnSendMsgBtnClicked()
{
    auto textEdit = ui->chatEdit;
    const QVector<MsgInfo> &msgList = textEdit->GetMsgList();
    if (msgList.isEmpty()) {
        return;
    }
    if (currentUid_ < 0) {
        LOG_WARN() << "send message failed, no friend selected";
        return;
    }

    auto selfInfo = UserManager::GetInstance().GetUserInfo();
    if (!selfInfo) {
        LOG_WARN() << "send message failed, user info is null";
        return;
    }
    QString selfName = UserManager::GetInstance().GetName();
    QString selfIcon = UserManager::GetInstance().GetIcon();
    if (selfIcon.isEmpty()) {
        selfIcon = kDefaultHead;
    }
    int selfUid = UserManager::GetInstance().GetUid();

    auto friendInfo = UserManager::GetInstance().GetFriendById(currentUid_);
    if (!friendInfo) {
        LOG_WARN() << "send message failed, friend not found, uid =" << currentUid_;
        return;
    }

    // 文本消息批量发送：累计长度超过上限时先 flush 一批
    QJsonArray textArray;
    int txtSize = 0;

    for (int i = 0; i < msgList.size(); ++i) {
        // 单条消息内容过长则跳过
        if (msgList[i].content.length() > kMaxSingleMsgLen) {
            LOG_WARN() << "skip message, content too long:" << msgList[i].content.length();
            continue;
        }

        const QString &type = msgList[i].msgFlag;
        LOG_DEBUG() << "send message type : " << type;

        ChatItemBase *item = new ChatItemBase(true);
        item->SetUserName(selfName);
        item->SetUserIcon(QPixmap(selfIcon));
        QWidget *bubble = nullptr;

        if (type == "text") {
            // 生成唯一消息 id
            QString uuidString = QUuid::createUuid().toString();

            bubble = new TextBubble(msgList[i].content, true);

            // 累计长度超限则先把已累计的文本发出去
            if (txtSize + msgList[i].content.length() > kMaxTextBatchSize) {
                ChatService::GetInstance().SendTextChatMsg(selfUid, currentUid_, textArray);
                txtSize = 0;
                textArray = QJsonArray();
            }

            txtSize += msgList[i].content.length();
            QJsonObject obj;
            obj["content"] = msgList[i].content;
            obj["msg_id"] = uuidString; 
            textArray.append(obj);

            // 写入好友历史消息，便于切回会话时重渲染
            auto chatData = std::make_shared<TextChatData>(uuidString, msgList[i].content, selfUid, currentUid_);
            friendInfo->AppendChatMsgs({chatData});
            friendInfo->last_msg_ = msgList[i].content;
        } else if (type == "image") {
            bubble = new PictureBubble(QPixmap(msgList[i].content), true);
        } else if (type == "file") {
            // 文件气泡暂未实现
            LOG_WARN() << "file message not implemented, skip";
        }

        if (bubble != nullptr) {
            item->SetWidget(bubble);
            ui->chatListWidget->AppendChatItem(item);
        }
    }

    // 发送剩余的文本消息
    if (!textArray.isEmpty()) {
        ChatService::GetInstance().SendTextChatMsg(selfUid, currentUid_, textArray);
    }
}
