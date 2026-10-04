#ifndef HTTPMANAGER_H
#define HTTPMANAGER_H

#include "noncopyable.h"
#include "constants.h"
#include <QString>
#include <QUrl>
#include <QObject>
#include <QNetworkAccessManager>
#include <memory>
#include <QJsonObject>

class HttpManager : public QObject {
    Q_OBJECT
public:
    static HttpManager &GetInstance()
    {
        static HttpManager instance;
        return instance;
    }
    ~HttpManager() = default;
    void PostHttpReq(QUrl url, QJsonObject json, RequestId reqId, Modules mod);
public Q_SLOTS:
    void SlotHttpFinish(RequestId id, QString res, ErrorCodes err, Modules mod);
Q_SIGNALS:
    void SigHttpFinish(RequestId id, QString res, ErrorCodes err, Modules mod);
    void SigRegisterModFinish(RequestId id, QString res, ErrorCodes err);
    void SigResetModFinish(RequestId id, QString res, ErrorCodes err);
    void SigLoginModFinish(RequestId id, QString res, ErrorCodes err);

private:
    DISALLOW_COPY_MOVE(HttpManager)
    HttpManager();
    QNetworkAccessManager manager_;
};

#endif // HTTPMANAGER_H
