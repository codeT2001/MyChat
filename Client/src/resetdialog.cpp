#include "resetdialog.h"
#include "ui_resetdialog.h"
#include "authservice.h"
#include "utils.h"
#include "logger.h"

ResetDialog::ResetDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ResetDialog)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/resetdialog.qss");
    SetupWindow();
    SetupConnections();
    SetupValidation();
    Utils::ClearTips();
}

ResetDialog::~ResetDialog()
{
    delete ui;
}

void ResetDialog::OnGetCodeClicked()
{
    LOG_DEBUG() << "verify btn clicked ";
    auto email = ui->email->text();
    if (!Utils::CheckEmailValid(email, ui->resetErrTip)) {
        return;
    }
    AuthService::GetInstance().GetResetVerifyCode(email);
}

void ResetDialog::OnSureBtnClicked()
{
    if (!CheckUserValid() || !CheckEmailValid() || !CheckPasswordValid() || !CheckVerifyCodeValid()) {
        return;
    }
    ui->sureBtn->setEnabled(false);
    AuthService::GetInstance().ResetPassword(ui->name->text(), ui->email->text(), ui->newPassword->text(),
                                             ui->verifyCode->text());
}

void ResetDialog::OnVerifyCodeResult(bool ok, const QString &msg)
{
    if (ok) {
        Utils::ShowTip(ui->resetErrTip, tr("验证码已发送到邮箱，注意查收"));
    } else {
        Utils::ShowTip(ui->resetErrTip, msg, true);
    }
}

void ResetDialog::OnResetResult(bool ok, const QString &msg)
{
    ui->sureBtn->setEnabled(true);
    if (!ok) {
        Utils::ShowTip(ui->resetErrTip, msg, true);
        return;
    }
    Utils::ShowTip(ui->resetErrTip, tr("重置成功,点击返回登录"));
}

bool ResetDialog::CheckUserValid()
{
    QString username = ui->name->text();
    return Utils::CheckUserValid(username, ui->resetErrTip);
}

bool ResetDialog::CheckPasswordValid()
{
    QString pwd = ui->newPassword->text();
    return Utils::CheckPasswordValid(pwd, ui->resetErrTip);
}

bool ResetDialog::CheckEmailValid()
{
    QString email = ui->email->text();
    return Utils::CheckEmailValid(email, ui->resetErrTip);
}

bool ResetDialog::CheckVerifyCodeValid()
{
    auto code = ui->verifyCode->text();
    return Utils::CheckVerifyCodeValid(code, ui->resetErrTip);
}

void ResetDialog::SetupWindow()
{
    setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
}

void ResetDialog::SetupConnections()
{
    connect(ui->sureBtn, &QPushButton::clicked, this, &ResetDialog::OnSureBtnClicked);
    connect(ui->backBtn, &QPushButton::clicked, this, &ResetDialog::SigSwitchLogin);
    connect(ui->getCode, &QPushButton::clicked, this, &ResetDialog::OnGetCodeClicked);
    auto &authService = AuthService::GetInstance();
    connect(&authService, &AuthService::SigResetVerifyCodeResult, this, &ResetDialog::OnVerifyCodeResult);
    connect(&authService, &AuthService::SigResetResult, this, &ResetDialog::OnResetResult);
}

void ResetDialog::SetupValidation()
{
    connect(ui->name, &QLineEdit::editingFinished, this, &ResetDialog::CheckUserValid);
    connect(ui->email, &QLineEdit::editingFinished, this, &ResetDialog::CheckEmailValid);
    connect(ui->newPassword, &QLineEdit::editingFinished, this, &ResetDialog::CheckPasswordValid);
    connect(ui->verifyCode, &QLineEdit::editingFinished, this, &ResetDialog::CheckVerifyCodeValid);
}
