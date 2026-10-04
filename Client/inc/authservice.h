#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include "constants.h"

class QJsonObject;

// 认证业务服务层（单例）：统一承接登录、注册、重置密码三类认证流程，
// 包括 HTTP 请求、TCP 连接与聊天服务器登录的完整链路。
// UI 只通过本服务发起认证请求并监听结果信号，不直接依赖 HttpManager/TcpManager。
class AuthService : public QObject {
    Q_OBJECT

public:
    static AuthService &GetInstance()
    {
        static AuthService instance;
        return instance;
    }
    ~AuthService() = default;

    // 登录：HTTP 换取 token/host/port → TCP 连接 ChatServer → 聊天服务器登录
    void Login(const QString &user, const QString &pwd);

    // 注册流程
    void GetRegisterVerifyCode(const QString &email);
    void Register(const QString &name, const QString &email, const QString &pwd, const QString &verifyCode);

    // 重置密码流程
    void GetResetVerifyCode(const QString &email);
    void ResetPassword(const QString &name, const QString &email, const QString &pwd, const QString &verifyCode);

signals:
    void sigLoginSuccess();
    void sigLoginFailed();
    void sigLoginError(const QString &msg);

    // 以下结果信号：ok 为 true 时 msg 为空；失败时 msg 为可直接展示的错误文案
    void sigRegisterVerifyCodeResult(bool ok, const QString &msg);
    void sigRegisterResult(bool ok, const QString &msg);
    void sigResetVerifyCodeResult(bool ok, const QString &msg);
    void sigResetResult(bool ok, const QString &msg);

private slots:
    void OnHttpFinish(RequestId id, const QString &res, ErrorCodes err);
    void SlotTcpConnectFinish(bool success);
    void OnTcpMessageReceived(RequestId id, const QByteArray &data);

private:
    AuthService();
    AuthService(const AuthService &) = delete;
    AuthService &operator=(const AuthService &) = delete;

    void PostHttp(const QString &path, const QJsonObject &json, RequestId id);
    void EmitHttpError(RequestId id, const QString &msg);
    void HandleLoginHttpRsp(const QJsonObject &obj);

    // 注册与重置共用"获取验证码"接口，用 pending 流程区分回包归属
    enum class VerifyFlow { None, Register, Reset };

    uint32_t uid_ = 0;
    QString token_;
    VerifyFlow verifyFlow_ = VerifyFlow::None;
};

#endif // AUTHSERVICE_H
