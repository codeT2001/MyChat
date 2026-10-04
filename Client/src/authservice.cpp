#include "authservice.h"
#include "tcpmanager.h"
#include "httpmanager.h"
#include "usermanager.h"
#include "jsoncodec.h"
#include "userdata.h"
#include "utils.h"
#include "log.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

AuthService::AuthService()
{
    connect(&HttpManager::GetInstance(), &HttpManager::SigHttpFinish, this, &AuthService::OnHttpFinish);
    connect(&TcpManager::GetInstance(), &TcpManager::SigConnectionSuccess, this, &AuthService::SlotTcpConnectFinish);
    connect(&TcpManager::GetInstance(), &TcpManager::SigMessageReceived, this, &AuthService::OnTcpMessageReceived);
}

void AuthService::PostHttp(const QString &path, const QJsonObject &json, RequestId id)
{
    HttpManager::GetInstance().PostHttpReq(QUrl(Utils::GetServerUrl(path)), json, id);
}

void AuthService::Login(const QString &user, const QString &pwd)
{
    LOG_DEBUG() << "AuthService::Login";
    QJsonObject jsonObj;
    jsonObj["name"] = user;
    jsonObj["passwd"] = pwd;
    PostHttp(HttpPaths::USER_LOGIN, jsonObj, RequestId::USER_LOGIN);
}

void AuthService::GetRegisterVerifyCode(const QString &email)
{
    verifyFlow_ = VerifyFlow::Register;
    QJsonObject jsonObj;
    jsonObj["email"] = email;
    PostHttp(HttpPaths::GET_VERIFY_CODE, jsonObj, RequestId::GET_VERIFY_CODE);
}

void AuthService::Register(const QString &name, const QString &email, const QString &pwd, const QString &verifyCode)
{
    QJsonObject jsonObj;
    jsonObj["name"] = name;
    jsonObj["email"] = email;
    jsonObj["passwd"] = pwd;
    jsonObj["verifyCode"] = verifyCode;
    PostHttp(HttpPaths::REGISTER_USER, jsonObj, RequestId::REG_USER);
}

void AuthService::GetResetVerifyCode(const QString &email)
{
    verifyFlow_ = VerifyFlow::Reset;
    QJsonObject jsonObj;
    jsonObj["email"] = email;
    PostHttp(HttpPaths::GET_VERIFY_CODE, jsonObj, RequestId::GET_VERIFY_CODE);
}

void AuthService::ResetPassword(const QString &name, const QString &email, const QString &pwd, const QString &verifyCode)
{
    QJsonObject jsonObj;
    jsonObj["name"] = name;
    jsonObj["email"] = email;
    jsonObj["passwd"] = pwd;
    jsonObj["verifyCode"] = verifyCode;
    PostHttp(HttpPaths::RESET_PASSWORD, jsonObj, RequestId::RESET_PASSWORD);
}

void AuthService::OnHttpFinish(RequestId id, const QString &res, ErrorCodes err)
{
    if (err != ErrorCodes::SUCCESS) {
        EmitHttpError(id, tr("网络请求错误"));
        return;
    }

    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        EmitHttpError(id, tr("json解析错误"));
        return;
    }

    QJsonObject obj = jsonDoc.object();
    if (id == RequestId::USER_LOGIN) {
        // 登录回包结构不同（含 host/port/token），单独走完整解析
        HandleLoginHttpRsp(obj);
        return;
    }

    bool ok = obj["error"].toInt() == static_cast<int>(ErrorCodes::SUCCESS);
    QString msg = ok ? QString() : tr("参数错误");

    switch (id) {
        case RequestId::GET_VERIFY_CODE: {
            if (verifyFlow_ == VerifyFlow::Register) {
                emit sigRegisterVerifyCodeResult(ok, msg);
            } else if (verifyFlow_ == VerifyFlow::Reset) {
                emit sigResetVerifyCodeResult(ok, msg);
            }
            verifyFlow_ = VerifyFlow::None;
            break;
        }
        case RequestId::REG_USER:
            emit sigRegisterResult(ok, msg);
            break;
        case RequestId::RESET_PASSWORD:
            emit sigResetResult(ok, msg);
            break;
        default:
            break;
    }
}

void AuthService::EmitHttpError(RequestId id, const QString &msg)
{
    switch (id) {
        case RequestId::USER_LOGIN:
            emit sigLoginError(msg);
            break;
        case RequestId::GET_VERIFY_CODE:
            if (verifyFlow_ == VerifyFlow::Register) {
                emit sigRegisterVerifyCodeResult(false, msg);
            } else if (verifyFlow_ == VerifyFlow::Reset) {
                emit sigResetVerifyCodeResult(false, msg);
            }
            verifyFlow_ = VerifyFlow::None;
            break;
        case RequestId::REG_USER:
            emit sigRegisterResult(false, msg);
            break;
        case RequestId::RESET_PASSWORD:
            emit sigResetResult(false, msg);
            break;
        default:
            break;
    }
}

void AuthService::HandleLoginHttpRsp(const QJsonObject &obj)
{
    int error = obj["error"].toInt();
    if (error != static_cast<int32_t>(ErrorCodes::SUCCESS)) {
        emit sigLoginError(tr("参数错误"));
        return;
    }
    ServerInfo info = JsonParser::ParseLoginHttpRsp(obj);

    uid_ = info.uid;
    token_ = info.token;

    LOG_INFO() << "AuthService: HTTP login ok, connecting to" << info.host << info.port;
    // 发起 TCP 连接
    TcpManager::GetInstance().SlotTcpConnect(info);
}

void AuthService::SlotTcpConnectFinish(bool success)
{
    if (success) {
        LOG_INFO() << "AuthService: TCP connected, sending CHAT_LOGIN";
        QByteArray data = JsonSerializer::SerializeChatLoginReq(static_cast<int>(uid_), token_);
        TcpManager::GetInstance().SlotSendData(RequestId::CHAT_LOGIN_REQ, data);
    } else {
        emit sigLoginError(tr("网络异常"));
    }
}

void AuthService::OnTcpMessageReceived(RequestId id, const QByteArray &data)
{
    if (id == RequestId::NOTIFY_KICK) {
        LOG_WARN() << "kicked by server: account logged in elsewhere";
        // 立即断开并禁止自动重连——重连也无法恢复登录态（会话已被新登录顶掉）
        TcpManager::GetInstance().Disconnect();
        emit sigKicked(tr("您的账号在其他设备登录，您已被迫下线"));
        return;
    }
    if (id != RequestId::CHAT_LOGIN_RSP) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        LOG_WARN() << "AuthService: CHAT_LOGIN_RSP parse failed";
        emit sigLoginFailed();
        return;
    }
    QJsonObject obj = doc.object();
    if (ExtractError(obj) != ErrorCodes::SUCCESS) {
        LOG_WARN() << "AuthService: CHAT_LOGIN_RSP error";
        emit sigLoginFailed();
        return;
    }

    auto &uMgr = UserManager::GetInstance();
    uMgr.SetUserInfo(JsonParser::ParseUserInfo(obj));
    uMgr.SetToken(obj["token"].toString());
    if (obj.contains("apply_list")) {
        uMgr.AppendApplyList(JsonParser::ParseApplyList(obj["apply_list"].toArray()));
    }
    if (obj.contains("friend_list")) {
        uMgr.AppendFriendList(JsonParser::ParseFriendList(obj["friend_list"].toArray()));
    }

    LOG_INFO() << "AuthService: login success";
    emit sigLoginSuccess();
}
