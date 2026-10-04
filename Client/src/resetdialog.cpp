#include "resetdialog.h"
#include "ui_resetdialog.h"
#include "utils.h"
#include "httpmanager.h"
#include "log.h"

ResetDialog::ResetDialog(QWidget *parent) : QDialog(parent), ui(new Ui::ResetDialog)
{
    ui->setupUi(this);
    Utils::LoadQss(this, ":/style/resetdialog.qss");
    SetupWindow();
    SetupConnections();
    SetupValidation();
    Utils::ClearTips();
    InitHttpHandles();
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
    QJsonObject jsonObj;
    jsonObj["email"] = email;
    HttpManager::GetInstance().PostHttpReq(QUrl(Utils::GetServerUrl(HttpPaths::GET_VERIFY_CODE)), jsonObj,
                                           RequestId::GET_VERIFY_CODE, Modules::RESET);
}

void ResetDialog::OnSureBtnClicked()
{
    if (!CheckUserValid() || !CheckEmailValid() || !CheckPasswordValid() || !CheckVerifyCodeValid()) {
        return;
    }
    // 发送http重置用户请求
    QJsonObject json_obj;
    json_obj["name"] = ui->name->text();
    json_obj["email"] = ui->email->text();
    json_obj["passwd"] = ui->newPassword->text();
    json_obj["verifyCode"] = ui->verifyCode->text();
    ui->sureBtn->setEnabled(false);
    HttpManager::GetInstance().PostHttpReq(QUrl(Utils::GetServerUrl(HttpPaths::RESET_PASSWORD)), json_obj,
                                           RequestId::RESET_PASSWORD, Modules::RESET);
}

void ResetDialog::SlotResetModFinish(RequestId id, QString res, ErrorCodes err)
{
    ui->sureBtn->setEnabled(true);
    if (err != ErrorCodes::SUCCESS) {
        Utils::ShowTip(ui->resetErrTip, tr("网络请求错误"), true);
        return;
    }

    // 解析 JSON 字符串,res需转化为QByteArray
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    // json解析错误
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        Utils::ShowTip(ui->resetErrTip, tr("json解析错误"), true);
        return;
    }
    if (handles_.find(id) != handles_.end()) {
        handles_[id](jsonDoc.object());
    }
}

void ResetDialog::InitHttpHandles()
{
    handles_.insert(RequestId::GET_VERIFY_CODE, [this](const QJsonObject &jsonObj) {
        int32_t error = jsonObj["error"].toInt();
        if (error != static_cast<int32_t>(ErrorCodes::SUCCESS)) {
            Utils::ShowTip(ui->resetErrTip, tr("参数错误"), true);
            return;
        }
        auto email = jsonObj["email"].toString();
        Utils::ShowTip(ui->resetErrTip, tr("验证码已发送到邮箱，注意查收"));
        LOG_DEBUG() << "email is " << email;
    });
    handles_.insert(RequestId::RESET_PASSWORD, [this](const QJsonObject &jsonObj) {
        int32_t error = jsonObj["error"].toInt();
        if (error != static_cast<int32_t>(ErrorCodes::SUCCESS)) {
            Utils::ShowTip(ui->resetErrTip, tr("参数错误"), true);
            return;
        }
        auto email = jsonObj["email"].toString();
        Utils::ShowTip(ui->resetErrTip, tr("重置成功,点击返回登录"));
        LOG_DEBUG() << "email is " << email;
        LOG_DEBUG() << "user uid is " << jsonObj["uid"].toInt();
    });
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
    connect(ui->backBtn, &QPushButton::clicked, this, &ResetDialog::SwitchLogin);
    connect(ui->getCode, &QPushButton::clicked, this, &ResetDialog::OnGetCodeClicked);
    connect(&HttpManager::GetInstance(), &HttpManager::SigResetModFinish, this, &ResetDialog::SlotResetModFinish);
}

void ResetDialog::SetupValidation()
{
    connect(ui->name, &QLineEdit::editingFinished, this, &ResetDialog::CheckUserValid);
    connect(ui->email, &QLineEdit::editingFinished, this, &ResetDialog::CheckEmailValid);
    connect(ui->newPassword, &QLineEdit::editingFinished, this, &ResetDialog::CheckPasswordValid);
    connect(ui->verifyCode, &QLineEdit::editingFinished, this, &ResetDialog::CheckVerifyCodeValid);
}
