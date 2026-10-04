#include "httpmanager.h"
#include "registerdialog.h"
#include "ui_registerdialog.h"
#include "utils.h"
#include "log.h"

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
    InitHttpHandles();
    Utils::ClearTips();
}

void RegisterDialog::SetupWindow()
{
    setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
}

void RegisterDialog::SetupConnections()
{
    connect(&HttpManager::GetInstance(), &HttpManager::SigRegisterModFinish, this,
            &RegisterDialog::SlotRegisterModFinish);

    connect(ui->getCode, &QPushButton::clicked, this, &RegisterDialog::OnGetCodeClicked);
    connect(ui->sure, &QPushButton::clicked, this, &RegisterDialog::OnSureBtnClicked);
    connect(ui->cancle, &QPushButton::clicked, this, &RegisterDialog::OnCancelClicked);
    connect(ui->backLoginBtn, &QPushButton::clicked, this, &RegisterDialog::OnBackToLoginClicked);
}

void RegisterDialog::SetupPasswordToggle()
{
    ui->passLabel->SetState("unvisible", "unvisible_hover", "", "visible", "visible_hover", "");
    ui->confirmLabel->SetState("unvisible", "unvisible_hover", "", "visible", "visible_hover", "");

    connect(ui->passLabel, &QClickLabel::clicked, this, [this]() {
        ui->password->setEchoMode(ui->passLabel->GetCurState() == ClickLabelState::NORMAL ? QLineEdit::Password
                                                                                          : QLineEdit::Normal);
    });

    connect(ui->confirmLabel, &QClickLabel::clicked, this, [this]() {
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
    emit SwitchLogin();
}

void RegisterDialog::OnBackToLoginClicked()
{
    timer_->stop();
    emit SwitchLogin();
}

void RegisterDialog::OnTimerTimeout()
{
    if (countDown_ == 0) {
        timer_->stop();
        emit SwitchLogin();
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

    // Send HTTP request to get verification code
    QJsonObject jsonObj;
    jsonObj["email"] = email;
    HttpManager::GetInstance().PostHttpReq(Utils::GetServerUrl(HttpPaths::GET_VERIFY_CODE), jsonObj,
                                           RequestId::GET_VERIFY_CODE, Modules::REGISTER);
}

void RegisterDialog::OnSureBtnClicked()
{
    if (!CheckUserValid() || !CheckEmailValid() || !CheckPasswordValid() || !CheckConfirmValid() ||
        !CheckVerifyCodeValid()) {
        return;
    }
    QJsonObject jsonObj;
    jsonObj["name"] = ui->user->text();
    jsonObj["email"] = ui->email->text();
    jsonObj["passwd"] = ui->password->text();
    jsonObj["verifyCode"] = ui->verifyCode->text();
    ui->sure->setEnabled(false);
    HttpManager::GetInstance().PostHttpReq(Utils::GetServerUrl(HttpPaths::REGISTER_USER), jsonObj, RequestId::REG_USER,
                                           Modules::REGISTER);
}

void RegisterDialog::SlotRegisterModFinish(RequestId id, QString res, ErrorCodes err)
{
    LOG_DEBUG() << "SlotRegisterModFinish";
    ui->sure->setEnabled(true);

    if (err != ErrorCodes::SUCCESS) {
        Utils::ShowTip(ui->regErrTip, tr("网络请求错误"), true);
        return;
    }

    // 解析 JSON 字符串,res需转化为QByteArray
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    // json解析错误
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        Utils::ShowTip(ui->regErrTip, tr("json解析错误"), true);
        return;
    }

    LOG_DEBUG() << static_cast<int32_t>(id);
    if (handles_.find(id) != handles_.end()) {
        handles_[id](jsonDoc.object());
    }
}

void RegisterDialog::InitHttpHandles()
{
    // 注册获取验证码回包逻辑
    handles_.insert(RequestId::GET_VERIFY_CODE, [this](const QJsonObject &jsonObj) {
        LOG_DEBUG() << "Handle GET_VERIFY_CODE";
        int error = jsonObj["error"].toInt();
        if (error != static_cast<int32_t>(ErrorCodes::SUCCESS)) {
            Utils::ShowTip(ui->regErrTip, tr("参数错误"), true);
            return;
        }
        auto email = jsonObj["email"].toString();
        Utils::ShowTip(ui->regErrTip, tr("验证码已发送到邮箱，注意查收"));
        LOG_DEBUG() << "email is " << email;
    });
    handles_.insert(RequestId::REG_USER, [this](const QJsonObject &jsonObj) {
        LOG_DEBUG() << "Handle REG_USER";
        int error = jsonObj["error"].toInt();
        if (error != static_cast<int32_t>(ErrorCodes::SUCCESS)) {
            Utils::ShowTip(ui->regErrTip, tr("参数错误"), true);
            return;
        }
        auto email = jsonObj["email"].toString();
        Utils::ShowTip(ui->regErrTip, tr("用户注册成功"));
        LOG_DEBUG() << "email is " << email;
        LOG_DEBUG() << "user uid is " << jsonObj["uid"].toInt();
        ChangeTipPage();
    });
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

void RegisterDialog::ChangeTipPage()
{
    timer_->stop();
    ui->stackedWidget->setCurrentWidget(ui->page_2);
    countDown_ = COUNTDOWN_SECONDS;
    auto str = QString("注册成功，%1 s后返回登录").arg(countDown_);
    ui->tipLb->setText(str);
    timer_->start(COUNTDOWN_INTERVAL_MS);
}
