#include "logindialog.h"
#include "authservice.h"
#include "ui_logindialog.h"
#include "utils.h"
#include "logger.h"

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::LoginDialog)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/logindialog.qss");
    SetupWindow();
    SetupConnections();
    SetupPasswordToggle();
    SetupValidation();
    Utils::ClearTips();
}

LoginDialog::~LoginDialog()
{
    delete ui;
}

bool LoginDialog::CheckUserValid()
{
    QString username = ui->userName->text();
    return Utils::CheckUserValid(username, ui->loginErrTip);
}

bool LoginDialog::CheckPasswordValid()
{
    QString pwd = ui->password->text();
    return Utils::CheckPasswordValid(pwd, ui->loginErrTip);
}

void LoginDialog::SetupWindow()
{
    setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
}

void LoginDialog::SetupConnections()
{
    connect(ui->registerBtn, &QPushButton::clicked, this, &LoginDialog::SigSwitchRegister);
    connect(ui->forgetLabel, &StatefulClickLabel::clicked, this, &LoginDialog::SigSwitchReset);
    connect(ui->loginBtn, &QPushButton::clicked, this, &LoginDialog::OnLoginBtnClicked);
    // 监听 AuthService 的结果信号（不再直接依赖 HttpManager/TcpManager）
    auto &authService = AuthService::GetInstance();
    connect(&authService, &AuthService::SigLoginFailed, this, &LoginDialog::OnLoginFailed);
    connect(&authService, &AuthService::SigLoginError, this, &LoginDialog::OnLoginError);
}

void LoginDialog::SetupPasswordToggle()
{
    ui->forgetLabel->SetStateStyles("normal", "hover");
    ui->loginPassLb->SetStateStyles("unvisible", "unvisible_hover", "", "visible", "visible_hover", "");
    connect(ui->loginPassLb, &StatefulClickLabel::clicked, this, [this]() {
        ui->password->setEchoMode(ui->loginPassLb->GetCurState() == ClickLabelState::NORMAL ? QLineEdit::Password
                                                                                            : QLineEdit::Normal);
    });
}

void LoginDialog::SetupValidation()
{
    connect(ui->userName, &QLineEdit::editingFinished, this, &LoginDialog::CheckUserValid);
    connect(ui->password, &QLineEdit::editingFinished, this, &LoginDialog::CheckPasswordValid);
}

void LoginDialog::OnLoginBtnClicked()
{
    LOG_DEBUG() << "login btn clicked";
    if (!CheckUserValid() || !CheckPasswordValid()) {
        return;
    }
    ui->loginBtn->setEnabled(false);
    AuthService::GetInstance().Login(ui->userName->text(), ui->password->text());
}

void LoginDialog::OnLoginFailed()
{
    ui->loginBtn->setEnabled(true);
    Utils::ShowTip(ui->loginErrTip, tr("登陆失败"), true);
}

void LoginDialog::OnLoginError(const QString &msg)
{
    ui->loginBtn->setEnabled(true);
    Utils::ShowTip(ui->loginErrTip, msg, true);
}
