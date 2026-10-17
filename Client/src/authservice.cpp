#include "authservice.h"
#include "tcpmanager.h"
#include "httpmanager.h"
#include "usermanager.h"
#include "jsoncodec.h"
#include "domainmodels.h"
#include "utils.h"
#include "logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {
// 错误码 → 用户可见文案：HTTP/TCP 回包统一走这里，避免一律提示"参数错误"
QString ErrorCodeToMessage(ErrorCodes err)
{
    switch (err) {
        case ErrorCodes::SUCCESS:
            return QString();
        case ErrorCodes::ERROR_JSON:
            return QObject::tr("请求参数有误");
        case ErrorCodes::RPC_FAILED:
            return QObject::tr("服务暂时不可用，请稍后重试");
        case ErrorCodes::VERIFYCODE_NOT_FOUND_OR_EXPIRED:
            return QObject::tr("验证码不存在或已过期");
        case ErrorCodes::USER_EXIST:
            return QObject::tr("用户名或邮箱已被注册");
        case ErrorCodes::EMAIL_NOT_MATCH:
            return QObject::tr("姓名与邮箱不匹配");
        case ErrorCodes::UPDATE_PASSWORD_FAILED:
            return QObject::tr("密码修改失败，请稍后重试");
        case ErrorCodes::PASSWORD_NOT_MATCH:
            return QObject::tr("用户名或密码错误");
        case ErrorCodes::UID_INVALID:
            return QObject::tr("用户不存在或已失效");
        case ErrorCodes::TOKEN_INVALID:
            return QObject::tr("登录凭证已失效，请重新登录");
        case ErrorCodes::USER_ALREADY_LOGIN:
            return QObject::tr("账号已在其他设备登录");
        case ErrorCodes::ERR_NETWORK:
            return QObject::tr("网络异常，请检查网络连接");
        case ErrorCodes::JSON_PARSE_ERROR:
            return QObject::tr("服务器响应解析失败");
        case ErrorCodes::VERIFY_CODE_EXISTS:
            return QObject::tr("验证码已发送，请稍后再试");
        case ErrorCodes::VERIFY_REDIS_ERROR:
        case ErrorCodes::VERIFY_MAIL_FAILED:
            return QObject::tr("验证码服务异常，请稍后重试");
        default:
            return QObject::tr("请求失败（错误码 %1）").arg(static_cast<int>(err));
    }
}
} // namespace

AuthService::AuthService()
{
    connect(&HttpManager::GetInstance(), &HttpManager::SigHttpFinish, this, &AuthService::OnHttpFinish);
    connect(&TcpManager::GetInstance(), &TcpManager::SigConnected, this, &AuthService::OnConnected);
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

    const ErrorCodes code = ExtractError(obj);
    const bool ok = (code == ErrorCodes::SUCCESS);
    const QString msg = ok ? QString() : ErrorCodeToMessage(code);

    switch (id) {
        case RequestId::GET_VERIFY_CODE: {
            if (verifyFlow_ == VerifyFlow::Register) {
                emit SigRegisterVerifyCodeResult(ok, msg);
            } else if (verifyFlow_ == VerifyFlow::Reset) {
                emit SigResetVerifyCodeResult(ok, msg);
            }
            verifyFlow_ = VerifyFlow::None;
            break;
        }
        case RequestId::REG_USER:
            emit SigRegisterResult(ok, msg);
            break;
        case RequestId::RESET_PASSWORD:
            emit SigResetResult(ok, msg);
            break;
        default:
            break;
    }
}

void AuthService::EmitHttpError(RequestId id, const QString &msg)
{
    switch (id) {
        case RequestId::USER_LOGIN:
            emit SigLoginError(msg);
            break;
        case RequestId::GET_VERIFY_CODE:
            if (verifyFlow_ == VerifyFlow::Register) {
                emit SigRegisterVerifyCodeResult(false, msg);
            } else if (verifyFlow_ == VerifyFlow::Reset) {
                emit SigResetVerifyCodeResult(false, msg);
            }
            verifyFlow_ = VerifyFlow::None;
            break;
        case RequestId::REG_USER:
            emit SigRegisterResult(false, msg);
            break;
        case RequestId::RESET_PASSWORD:
            emit SigResetResult(false, msg);
            break;
        default:
            break;
    }
}

void AuthService::HandleLoginHttpRsp(const QJsonObject &obj)
{
    const ErrorCodes code = ExtractError(obj);
    if (code != ErrorCodes::SUCCESS) {
        emit SigLoginError(ErrorCodeToMessage(code));
        return;
    }
    ServerInfo info = JsonParser::ParseLoginHttpRsp(obj);

    uid_ = info.uid;
    token_ = info.token;

    LOG_INFO() << "AuthService: HTTP login ok, connecting to" << info.host << info.port;
    // 发起 TCP 连接
    TcpManager::GetInstance().Connect(info);
}

void AuthService::OnConnected()
{
    LOG_INFO() << "AuthService: TCP connected, sending CHAT_LOGIN";
    QByteArray data = JsonSerializer::SerializeChatLoginReq(static_cast<int>(uid_), token_);
    TcpManager::GetInstance().Send(RequestId::CHAT_LOGIN_REQ, data);
}

void AuthService::OnTcpMessageReceived(RequestId id, const QByteArray &data)
{
    if (id == RequestId::NOTIFY_KICK) {
        LOG_WARN() << "kicked by server: account logged in elsewhere";
        // 立即断开并禁止自动重连——重连也无法恢复登录态（会话已被新登录顶掉）
        TcpManager::GetInstance().Disconnect();
        emit SigKicked(tr("您的账号在其他设备登录，您已被迫下线"));
        return;
    }
    if (id != RequestId::CHAT_LOGIN_RSP) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        LOG_WARN() << "AuthService: CHAT_LOGIN_RSP parse failed";
        emit SigLoginFailed();
        return;
    }
    QJsonObject obj = doc.object();
    if (ExtractError(obj) != ErrorCodes::SUCCESS) {
        LOG_WARN() << "AuthService: CHAT_LOGIN_RSP error";
        emit SigLoginFailed();
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
    emit SigLoginSuccess();
}
