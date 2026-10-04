#include "searchresultdialog.h"
#include "ui_searchresultdialog.h"
#include "domainmodels.h"
#include "friendrequestdialog.h"
#include "utils.h"
#include "logger.h"
SearchResultDialog::SearchResultDialog(bool success, QWidget *parent)
    : QDialog(parent), ui(new Ui::SearchResultDialog), parent_(parent)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/searchresultdialog.qss");
    setWindowTitle("添加");
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setModal(true);
    if (success) {
        ui->errTip->hide();
        connect(ui->addFriendBtn, &QPushButton::clicked, this, &SearchResultDialog::OnAddFriendBtnClicked);
    } else {
        Utils::ShowTip(ui->errTip, tr("查找失败，请检查uid是否正确！"), true);
        ui->iconLb->hide();
        ui->nameLb->hide();
        ui->addFriendBtn->setText(tr("返回"));
        connect(ui->addFriendBtn, &QPushButton::clicked, this, &SearchResultDialog::accept);
    }
    connect(this, &SearchResultDialog::accepted, this, [&]() { this->deleteLater(); });
}

SearchResultDialog::~SearchResultDialog()
{
    LOG_DEBUG() << "~SearchResultDialog";
    delete ui;
}

void SearchResultDialog::SetSearchInfo(std::shared_ptr<SearchInfo> info)
{
    ui->iconLb->setPixmap(Utils::RoundedAvatar(info->icon_, ui->iconLb->size()));
    ui->nameLb->setText(info->name_);
    info_ = info;
}

void SearchResultDialog::OnAddFriendBtnClicked()
{
    this->accept();
    auto requestDialog = new FriendRequestDialog(true, parent_);
    requestDialog->SetSearchInfo(info_);
    requestDialog->show();
}
