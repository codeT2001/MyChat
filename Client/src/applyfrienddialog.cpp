#include "applyfrienddialog.h"
#include "ui_applyfrienddialog.h"
#include "utils.h"
#include "friendservice.h"
#include "usermanager.h"
#include "userdata.h"
#include "log.h"
ApplyFriendDialog::ApplyFriendDialog(bool apply, QWidget *parent)
    : QWidget(parent), ui(new Ui::ApplyFriendDialog), isAuthMode_(!apply)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/applyfrienddialog.qss");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_StyledBackground);

    tipDatas_ = {"同学",          "家人",           "菜鸟教程",       "C++ Primer",
                 "Rust 程序设计", "父与子学Python", "nodejs开发指南", "go 语言开发指南",
                 "游戏伙伴",      "金融投资",       "微信读书",       "拼多多拼友"};

    ui->scrollArea->viewport()->installEventFilter(this);
    InitTipLbs();

    connect(ui->tipEdit, &QLineEdit::returnPressed, this, &ApplyFriendDialog::SlotLabelEnter);
    connect(ui->cancelBtn, &QPushButton::clicked, this, &ApplyFriendDialog::SlotCancel);
    if (apply) {
        connect(ui->sureBtn, &QPushButton::clicked, this, &ApplyFriendDialog::SlotApplySure);
    } else {
        ui->titleLabel->setText(tr("好友认证"));
        ui->cancelBtn->setText(tr("拒绝"));
        ui->sureBtn->setText(tr("同意"));
        connect(ui->sureBtn, &QPushButton::clicked, this, &ApplyFriendDialog::SlotAuthSure);
    }
}

ApplyFriendDialog::~ApplyFriendDialog()
{
    delete ui;
}

// 创建一个 FriendLabel 并完成通用初始化：new + SetText + show + 加入容器 + connect
// 返回裸指针，调用方负责选中态和 selectedTipLbs_ 的维护
FriendLabel *ApplyFriendDialog::CreateTipLabel(const QString &text)
{
    auto *lb = new FriendLabel(ui->scrollArea->widget());
    lb->SetText(text);
    lb->show();
    tipLbs_.push_back(lb);
    connect(lb, &FriendLabel::SigToggle, this, &ApplyFriendDialog::SlotChangeFriendLabelByTip);
    return lb;
}

void ApplyFriendDialog::InitTipLbs()
{
    for (const QString &text : tipDatas_) {
        CreateTipLabel(text);
    }
    RelayoutTipLbs();
}

void ApplyFriendDialog::RelayoutTipLbs()
{
    if (tipLbs_.empty()) {
        return;
    }

    const int margin = 10;
    const int hSpacing = 8;
    const int vSpacing = 8;

    int availableWidth = ui->scrollArea->viewport()->width() - 2 * margin;
    if (availableWidth <= margin) {
        return;
    }

    int currentX = margin;
    int currentY = margin;
    int rowMaxHeight = 0;

    for (FriendLabel *lb : tipLbs_) {
        int lbWidth = lb->Width();
        int lbHeight = lb->Height();

        if (currentX + lbWidth > availableWidth && currentX > margin) {
            currentX = margin;
            currentY += rowMaxHeight + vSpacing;
            rowMaxHeight = 0;
        }

        lb->move(currentX, currentY);

        currentX += lbWidth + hSpacing;
        rowMaxHeight = qMax(rowMaxHeight, lbHeight);
    }

    ui->scrollArea->widget()->setMinimumHeight(currentY + rowMaxHeight + margin);
}

bool ApplyFriendDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->scrollArea->viewport() && event->type() == QEvent::Resize) {
        RelayoutTipLbs();
    }
    return QWidget::eventFilter(obj, event);
}

void ApplyFriendDialog::SetSearchInfo(std::shared_ptr<SearchInfo> info)
{
    searchInfo_ = info;
}

void ApplyFriendDialog::SetApplyInfo(std::shared_ptr<ApplyInfo> info)
{
    applyInfo_ = info;
}

void ApplyFriendDialog::AddLabel(const QString &name)
{
    const QString text = name.trimmed();
    if (text.isEmpty()) {
        return;
    }

    // 同名标签已存在：直接置为选中，不重复创建
    for (FriendLabel *existing : tipLbs_) {
        if (existing->Text() == text) {
            existing->SetSelected(true);
            selectedTipLbs_.insert(text);
            return;
        }
    }

    auto *lb = CreateTipLabel(text);
    lb->SetSelected(true);
    selectedTipLbs_.insert(text);
    RelayoutTipLbs();
}

void ApplyFriendDialog::SlotLabelEnter()
{
    AddLabel(ui->tipEdit->text());
    ui->tipEdit->clear();
}

void ApplyFriendDialog::SlotChangeFriendLabelByTip(const QString &name, ClickLabelState state)
{
    if (state == ClickLabelState::SELECTED) {
        selectedTipLbs_.insert(name);
    } else {
        selectedTipLbs_.erase(name);
    }
}

void ApplyFriendDialog::SlotCancel()
{
    // 认证模式下，取消按钮即"拒绝"
    if (isAuthMode_ && applyInfo_) {
        // 通知服务端：from_uid=申请人, to_uid=当前拒绝者
        int uid = UserManager::GetInstance().GetUid();
        FriendService::GetInstance().RejectFriend(applyInfo_->uid_, uid);
        emit sigRejectFriend(applyInfo_);
    }
    this->hide();
    deleteLater();
}

void ApplyFriendDialog::SendFriendRequest(RequestId reqId, int fromUid, int toUid)
{
    const QString name = UserManager::GetInstance().GetName();

    auto desc = ui->applyEdit->text();
    if (desc.isEmpty()) {
        desc = ui->applyEdit->placeholderText();
    }

    auto backname = ui->remarkEdit->text();
    if (backname.isEmpty()) {
        backname = ui->remarkEdit->placeholderText();
    }

    if (reqId == RequestId::ADD_FRIEND_REQ) {
        FriendService::GetInstance().AddFriend(fromUid, toUid, name, desc, backname);
    } else if (reqId == RequestId::AUTH_FRIEND_REQ) {
        FriendService::GetInstance().AuthFriend(fromUid, toUid, name, desc, backname);
    }
}

void ApplyFriendDialog::SlotApplySure()
{
    LOG_DEBUG() << "Slot Apply Sure called";
    if (!searchInfo_) {
        LOG_WARN() << "SlotApplySure: searchInfo_ is null";
        return;
    }
    auto uid = UserManager::GetInstance().GetUid();
    SendFriendRequest(RequestId::ADD_FRIEND_REQ, uid, searchInfo_->uid_);
    this->hide();
    deleteLater();
}

void ApplyFriendDialog::SlotAuthSure()
{
    LOG_DEBUG() << "Slot Auth Sure called";
    if (!applyInfo_) {
        LOG_WARN() << "SlotAuthSure: applyInfo_ is null";
        return;
    }
    auto uid = UserManager::GetInstance().GetUid();
    SendFriendRequest(RequestId::AUTH_FRIEND_REQ, applyInfo_->uid_, uid);
    this->hide();
    deleteLater();
}
