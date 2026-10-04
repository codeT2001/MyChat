#include "friendrequestdialog.h"
#include "ui_friendrequestdialog.h"
#include "utils.h"
#include "friendservice.h"
#include "usermanager.h"
#include "domainmodels.h"
#include "logger.h"
FriendRequestDialog::FriendRequestDialog(bool applyMode, QWidget *parent)
    : QWidget(parent), ui(new Ui::FriendRequestDialog), isAuthMode_(!applyMode)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/friendrequestdialog.qss");
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_StyledBackground);

    presetTags_ = {"同学",          "家人",           "菜鸟教程",       "C++ Primer",
                   "Rust 程序设计", "父与子学Python", "nodejs开发指南", "go 语言开发指南",
                   "游戏伙伴",      "金融投资",       "微信读书",       "拼多多拼友"};

    ui->scrollArea->viewport()->installEventFilter(this);
    InitPresetTags();

    connect(ui->tipEdit, &QLineEdit::returnPressed, this, &FriendRequestDialog::OnTagEditReturnPressed);
    connect(ui->cancelBtn, &QPushButton::clicked, this, &FriendRequestDialog::OnCancel);
    if (applyMode) {
        connect(ui->sureBtn, &QPushButton::clicked, this, &FriendRequestDialog::OnApplySure);
    } else {
        ui->titleLabel->setText(tr("好友认证"));
        ui->cancelBtn->setText(tr("拒绝"));
        ui->sureBtn->setText(tr("同意"));
        connect(ui->sureBtn, &QPushButton::clicked, this, &FriendRequestDialog::OnAcceptSure);
    }
}

FriendRequestDialog::~FriendRequestDialog()
{
    delete ui;
}

// 创建一个 TagLabel 并完成通用初始化：new + SetText + show + 加入容器 + connect
// 返回裸指针，调用方负责选中态和 selectedTags_ 的维护
TagLabel *FriendRequestDialog::CreateTagLabel(const QString &text)
{
    auto *lb = new TagLabel(ui->scrollArea->widget());
    lb->SetText(text);
    lb->show();
    tagLabels_.push_back(lb);
    connect(lb, &TagLabel::SigToggle, this, &FriendRequestDialog::OnTagToggled);
    return lb;
}

void FriendRequestDialog::InitPresetTags()
{
    for (const QString &text : presetTags_) {
        CreateTagLabel(text);
    }
    RelayoutTags();
}

void FriendRequestDialog::RelayoutTags()
{
    if (tagLabels_.empty()) {
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

    for (TagLabel *lb : tagLabels_) {
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

bool FriendRequestDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->scrollArea->viewport() && event->type() == QEvent::Resize) {
        RelayoutTags();
    }
    return QWidget::eventFilter(obj, event);
}

void FriendRequestDialog::SetSearchInfo(std::shared_ptr<SearchInfo> info)
{
    searchInfo_ = info;
}

void FriendRequestDialog::SetApplyInfo(std::shared_ptr<ApplyInfo> info)
{
    applyInfo_ = info;
}

void FriendRequestDialog::AddTag(const QString &name)
{
    const QString text = name.trimmed();
    if (text.isEmpty()) {
        return;
    }

    // 同名标签已存在：直接置为选中，不重复创建
    for (TagLabel *existing : tagLabels_) {
        if (existing->Text() == text) {
            existing->SetSelected(true);
            selectedTags_.insert(text);
            return;
        }
    }

    auto *lb = CreateTagLabel(text);
    lb->SetSelected(true);
    selectedTags_.insert(text);
    RelayoutTags();
}

void FriendRequestDialog::OnTagEditReturnPressed()
{
    AddTag(ui->tipEdit->text());
    ui->tipEdit->clear();
}

void FriendRequestDialog::OnTagToggled(const QString &name, ClickLabelState state)
{
    if (state == ClickLabelState::SELECTED) {
        selectedTags_.insert(name);
    } else {
        selectedTags_.erase(name);
    }
}

void FriendRequestDialog::OnCancel()
{
    // 认证模式下，取消按钮即"拒绝"
    if (isAuthMode_ && applyInfo_) {
        // 通知服务端：from_uid=申请人, to_uid=当前拒绝者
        int uid = UserManager::GetInstance().GetUid();
        FriendService::GetInstance().RejectFriend(applyInfo_->uid_, uid);
        emit SigFriendRejected(applyInfo_);
    }
    this->hide();
    deleteLater();
}

void FriendRequestDialog::SendFriendRequest(RequestId reqId, int fromUid, int toUid)
{
    const QString name = UserManager::GetInstance().GetName();

    auto desc = ui->applyEdit->text();
    if (desc.isEmpty()) {
        desc = ui->applyEdit->placeholderText();
    }

    auto remark = ui->remarkEdit->text();
    if (remark.isEmpty()) {
        remark = ui->remarkEdit->placeholderText();
    }

    if (reqId == RequestId::ADD_FRIEND_REQ) {
        FriendService::GetInstance().AddFriend(fromUid, toUid, name, desc, remark);
    } else if (reqId == RequestId::AUTH_FRIEND_REQ) {
        FriendService::GetInstance().AcceptFriend(fromUid, toUid, name, desc, remark);
    }
}

void FriendRequestDialog::OnApplySure()
{
    LOG_DEBUG() << "Slot Apply Sure called";
    if (!searchInfo_) {
        LOG_WARN() << "OnApplySure: searchInfo_ is null";
        return;
    }
    auto uid = UserManager::GetInstance().GetUid();
    SendFriendRequest(RequestId::ADD_FRIEND_REQ, uid, searchInfo_->uid_);
    this->hide();
    deleteLater();
}

void FriendRequestDialog::OnAcceptSure()
{
    LOG_DEBUG() << "Slot Auth Sure called";
    if (!applyInfo_) {
        LOG_WARN() << "OnAcceptSure: applyInfo_ is null";
        return;
    }
    auto uid = UserManager::GetInstance().GetUid();
    SendFriendRequest(RequestId::AUTH_FRIEND_REQ, applyInfo_->uid_, uid);
    this->hide();
    deleteLater();
}
