#ifndef HTTPMANAGER_H
#define HTTPMANAGER_H

#include "constants.h"
#include <QString>
#include <QUrl>
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>

// HTTP 传输层单例：只负责发请求、收响应，按 RequestId 原样抛出 SigHttpFinish。
// 不感知任何业务模块（登录/注册/重置），业务路由由各 service 层自行完成。
class HttpManager : public QObject {
    Q_OBJECT
public:
    static HttpManager &GetInstance()
    {
        static HttpManager instance;
        return instance;
    }
    ~HttpManager() = default;
    void PostHttpReq(QUrl url, QJsonObject json, RequestId reqId);
Q_SIGNALS:
    void SigHttpFinish(RequestId id, QString res, ErrorCodes err);

private:
    HttpManager() = default;
    QNetworkAccessManager manager_;
};

#endif // HTTPMANAGER_H
