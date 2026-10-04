#include "findsuccessdialog.h"
#include "ui_findsuccessdialog.h"
#include "userdata.h"
#include "applyfrienddialog.h"
#include "utils.h"
#include "log.h"
FindSuccessDialog::FindSuccessDialog(bool success, QWidget *parent)
    : QDialog(parent), ui(new Ui::FindSuccessDialog), parent_(parent)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/findsuccessdialog.qss");
    setWindowTitle("添加");
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setModal(true);
    if (success) {
        ui->errTip->hide();
        connect(ui->addFriendBtn, &QPushButton::clicked, this, &FindSuccessDialog::OnAddFriendBtnClicked);
    } else {
        Utils::ShowTip(ui->errTip, tr("查找失败，请检查uid是否正确！"), true);
        ui->iconLb->hide();
        ui->nameLb->hide();
        ui->addFriendBtn->setText(tr("返回"));
        connect(ui->addFriendBtn, &QPushButton::clicked, this, &FindSuccessDialog::accept);
    }
    connect(this, &FindSuccessDialog::accepted, this, [&]() { this->deleteLater(); });
}

FindSuccessDialog::~FindSuccessDialog()
{
    LOG_DEBUG() << "~FindSuccessDialog";
    delete ui;
}

void FindSuccessDialog::SetSearchInfo(std::shared_ptr<SearchInfo> info)
{
    QPixmap pix(":/images/head_1.jpg");
    pix = pix.scaled(ui->iconLb->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->iconLb->setPixmap(pix);
    ui->nameLb->setText(info->name_);
    info_ = info;
}

void FindSuccessDialog::OnAddFriendBtnClicked()
{
    this->accept();
    auto applyFriendDialog = new ApplyFriendDialog(true, parent_);
    applyFriendDialog->SetSearchInfo(info_);
    applyFriendDialog->show();
}
