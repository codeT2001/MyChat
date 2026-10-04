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

AuthService::AuthService(QObject *parent) : QObject(parent), uid_(0)
{
    InitHandles();

    connect(&HttpManager::GetInstance(), &HttpManager::SigLoginModFinish, this, &AuthService::SlotHttpLoginFinish);
    connect(&TcpManager::GetInstance(), &TcpManager::SigConnectionSuccess, this, &AuthService::SlotTcpConnectFinish);
    connect(&TcpManager::GetInstance(), &TcpManager::SigMessageReceived, this, &AuthService::OnTcpMessageReceived);
}

void AuthService::InitHandles()
{
    handles_.insert(RequestId::USER_LOGIN, [this](const QJsonObject &jsonObj) {
        int error = jsonObj["error"].toInt();
        if (error != static_cast<int32_t>(ErrorCodes::SUCCESS)) {
            emit sigLoginError(tr("参数错误"));
            return;
        }
        ServerInfo info = JsonParser::ParseLoginHttpRsp(jsonObj);

        uid_ = info.uid;
        token_ = info.token;

        LOG_INFO() << "AuthService: HTTP login ok, connecting to" << info.host << info.port;
        // 直接调用 SlotTcpConnect 发起 TCP 连接
        TcpManager::GetInstance().SlotTcpConnect(info);
    });
}

void AuthService::Login(const QString &user, const QString &pwd)
{
    LOG_DEBUG() << "AuthService::Login";
    QJsonObject jsonObj;
    jsonObj["name"] = user;
    jsonObj["passwd"] = pwd;
    HttpManager::GetInstance().PostHttpReq(QUrl(Utils::GetServerUrl(HttpPaths::USER_LOGIN)), jsonObj,
                                           RequestId::USER_LOGIN, Modules::LOGIN);
}

void AuthService::SlotHttpLoginFinish(RequestId id, QString res, ErrorCodes err)
{
    if (err != ErrorCodes::SUCCESS) {
        emit sigLoginError(tr("网络请求错误"));
        return;
    }

    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    if (jsonDoc.isNull() || !jsonDoc.isObject()) {
        emit sigLoginError(tr("json解析错误"));
        return;
    }

    auto iter = handles_.find(id);
    if (iter != handles_.end()) {
        iter.value()(jsonDoc.object());
    }
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
