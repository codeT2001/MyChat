#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include <QObject>
#include <QMap>
#include <QByteArray>
#include <functional>
#include "constants.h"

class AuthService : public QObject {
    Q_OBJECT

public:
    explicit AuthService(QObject *parent = nullptr);
    ~AuthService() = default;

public slots:
    void Login(const QString &user, const QString &pwd);

signals:
    void sigLoginSuccess();
    void sigLoginFailed();
    void sigLoginError(const QString &msg);

private slots:
    void SlotHttpLoginFinish(RequestId id, QString res, ErrorCodes err);
    void SlotTcpConnectFinish(bool success);
    void OnTcpMessageReceived(RequestId id, const QByteArray &data);

private:
    void InitHandles();

    QMap<RequestId, std::function<void(const QJsonObject &)>> handles_;
    uint32_t uid_;
    QString token_;
};

#endif // AUTHSERVICE_H
