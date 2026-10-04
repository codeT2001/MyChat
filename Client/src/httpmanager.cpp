#include "httpmanager.h"
#include "log.h"
#include <QNetworkReply>

HttpManager::HttpManager()
{
    connect(this, &HttpManager::SigHttpFinish, this, &HttpManager::SlotHttpFinish);
}

void HttpManager::PostHttpReq(QUrl url, QJsonObject json, RequestId reqId, Modules mod)
{
    QByteArray data = QJsonDocument(json).toJson();
    // 通过url构造请求
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));
    // 发送请求，并处理响应。HttpManager 是单例，生命周期与程序一致，直接捕获 this 安全。
    QNetworkReply *reply = manager_.post(request, data);
    // 设置信号和槽等待发送完成
    QObject::connect(reply, &QNetworkReply::finished, [reply, this, reqId, mod]() {
        // 处理错误的情况
        LOG_DEBUG() << "QNetworkReply::finished";
        if (reply->error() != QNetworkReply::NoError) {
            LOG_WARN() << reply->errorString();
            // 发送信号通知完成
            emit this->SigHttpFinish(reqId, "", ErrorCodes::ERR_NETWORK, mod);
        } else { // 无错误则读回请求
            QString res = reply->readAll();
            LOG_DEBUG() << res;
            // 发送信号通知完成
            emit this->SigHttpFinish(reqId, res, ErrorCodes::SUCCESS, mod);
        }
        reply->deleteLater();
    });
}

void HttpManager::SlotHttpFinish(RequestId id, QString res, ErrorCodes err, Modules mod)
{
    if (mod == Modules::REGISTER) {
        emit SigRegisterModFinish(id, res, err);
    } else if (mod == Modules::RESET) {
        emit SigResetModFinish(id, res, err);
    } else if (mod == Modules::LOGIN) {
        emit SigLoginModFinish(id, res, err);
    }
}
