#include "authservice.h"
#include "registerdialog.h"
#include "ui_registerdialog.h"
#include "utils.h"
#include "logger.h"

namespace {
// Configuration constants
constexpr int32_t COUNTDOWN_SECONDS = 5;
constexpr int32_t COUNTDOWN_INTERVAL_MS = 1000;
} // namespace
RegisterDialog::RegisterDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::RegisterDialog), timer_(new QTimer(this)), countDown_(0)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/registerdialog.qss");
    SetupWindow();
    SetupConnections();
    SetupPasswordToggle();
    SetupValidation();
    SetupTimer();
    Utils::ClearTips();
}

void RegisterDialog::SetupWindow()
{
    setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
}

void RegisterDialog::SetupConnections()
{
    auto &authService = AuthService::GetInstance();
    connect(&authService, &AuthService::SigRegisterVerifyCodeResult, this, &RegisterDialog::OnVerifyCodeResult);
    connect(&authService, &AuthService::SigRegisterResult, this, &RegisterDialog::OnRegisterResult);

    connect(ui->getCode, &QPushButton::clicked, this, &RegisterDialog::OnGetCodeClicked);
    connect(ui->sure, &QPushButton::clicked, this, &RegisterDialog::OnSureBtnClicked);
    connect(ui->cancelBtn, &QPushButton::clicked, this, &RegisterDialog::OnCancelClicked);
    connect(ui->backLoginBtn, &QPushButton::clicked, this, &RegisterDialog::OnBackToLoginClicked);
}

void RegisterDialog::SetupPasswordToggle()
{
    ui->passLabel->SetStateStyles("unvisible", "unvisible_hover", "", "visible", "visible_hover", "");
    ui->confirmLabel->SetStateStyles("unvisible", "unvisible_hover", "", "visible", "visible_hover", "");

    connect(ui->passLabel, &StatefulClickLabel::clicked, this, [this]() {
        ui->password->setEchoMode(ui->passLabel->GetCurState() == ClickLabelState::NORMAL ? QLineEdit::Password
                                                                                          : QLineEdit::Normal);
    });

    connect(ui->confirmLabel, &StatefulClickLabel::clicked, this, [this]() {
        ui->confirm->setEchoMode(ui->confirmLabel->GetCurState() == ClickLabelState::NORMAL ? QLineEdit::Password
                                                                                            : QLineEdit::Normal);
    });
}

void RegisterDialog::SetupValidation()
{
    connect(ui->user, &QLineEdit::editingFinished, this, &RegisterDialog::CheckUserValid);
    connect(ui->email, &QLineEdit::editingFinished, this, &RegisterDialog::CheckEmailValid);
    connect(ui->password, &QLineEdit::editingFinished, this, &RegisterDialog::CheckPasswordValid);
    connect(ui->confirm, &QLineEdit::editingFinished, this, &RegisterDialog::CheckConfirmValid);
    connect(ui->verifyCode, &QLineEdit::editingFinished, this, &RegisterDialog::CheckVerifyCodeValid);
}

void RegisterDialog::SetupTimer()
{
    timer_->setInterval(COUNTDOWN_INTERVAL_MS);
    connect(timer_, &QTimer::timeout, this, &RegisterDialog::OnTimerTimeout);
}

void RegisterDialog::OnCancelClicked()
{
    timer_->stop();
    emit SigSwitchLogin();
}

void RegisterDialog::OnBackToLoginClicked()
{
    timer_->stop();
    emit SigSwitchLogin();
}

void RegisterDialog::OnTimerTimeout()
{
    if (countDown_ == 0) {
        timer_->stop();
        emit SigSwitchLogin();
        return;
    }

    countDown_--;
    auto str = QString("注册成功，%1 s后返回登录").arg(countDown_);
    ui->tipLb->setText(str);
}

RegisterDialog::~RegisterDialog()
{
    delete ui;
}

void RegisterDialog::OnGetCodeClicked()
{
    QString email = ui->email->text();

    if (!Utils::IsEmailValid(email)) {
        Utils::ShowTip(ui->regErrTip, tr("邮箱地址格式不正确"), true);
        return;
    }

    AuthService::GetInstance().GetRegisterVerifyCode(email);
}

void RegisterDialog::OnSureBtnClicked()
{
    if (!CheckUserValid() || !CheckEmailValid() || !CheckPasswordValid() || !CheckConfirmValid() ||
        !CheckVerifyCodeValid()) {
        return;
    }
    ui->sure->setEnabled(false);
    AuthService::GetInstance().Register(ui->user->text(), ui->email->text(), ui->password->text(),
                                        ui->verifyCode->text());
}

void RegisterDialog::OnVerifyCodeResult(bool ok, const QString &msg)
{
    LOG_DEBUG() << "OnVerifyCodeResult ok =" << ok;
    if (ok) {
        Utils::ShowTip(ui->regErrTip, tr("验证码已发送到邮箱，注意查收"));
    } else {
        Utils::ShowTip(ui->regErrTip, msg, true);
    }
}

void RegisterDialog::OnRegisterResult(bool ok, const QString &msg)
{
    LOG_DEBUG() << "OnRegisterResult ok =" << ok;
    ui->sure->setEnabled(true);
    if (!ok) {
        Utils::ShowTip(ui->regErrTip, msg, true);
        return;
    }
    Utils::ShowTip(ui->regErrTip, tr("用户注册成功"));
    ShowRegisterSuccessPage();
}

bool RegisterDialog::CheckUserValid()
{
    QString username = ui->user->text();
    return Utils::CheckUserValid(username, ui->regErrTip);
}

bool RegisterDialog::CheckPasswordValid()
{
    QString pwd = ui->password->text();
    return Utils::CheckPasswordValid(pwd, ui->regErrTip);
}

bool RegisterDialog::CheckConfirmValid()
{
    QString pwd = ui->password->text();
    QString confirm = ui->confirm->text();
    return Utils::CheckConfirmValid(pwd, confirm, ui->regErrTip);
}

bool RegisterDialog::CheckEmailValid()
{
    QString email = ui->email->text();
    return Utils::CheckEmailValid(email, ui->regErrTip);
}

bool RegisterDialog::CheckVerifyCodeValid()
{
    auto code = ui->verifyCode->text();
    return Utils::CheckVerifyCodeValid(code, ui->regErrTip);
}

void RegisterDialog::ShowRegisterSuccessPage()
{
    timer_->stop();
    ui->stackedWidget->setCurrentWidget(ui->page_2);
    countDown_ = COUNTDOWN_SECONDS;
    auto str = QString("注册成功，%1 s后返回登录").arg(countDown_);
    ui->tipLb->setText(str);
    timer_->start(COUNTDOWN_INTERVAL_MS);
}
