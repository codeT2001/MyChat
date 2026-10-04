#include "httpmanager.h"
#include "logger.h"
#include <QJsonDocument>
#include <QNetworkReply>

void HttpManager::PostHttpReq(QUrl url, QJsonObject json, RequestId reqId)
{
    QByteArray data = QJsonDocument(json).toJson();
    // 通过url构造请求
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));
    // 发送请求，并处理响应。HttpManager 是单例，生命周期与程序一致，直接捕获 this 安全。
    QNetworkReply *reply = manager_.post(request, data);
    // 设置信号和槽等待发送完成
    QObject::connect(reply, &QNetworkReply::finished, [reply, this, reqId]() {
        // 处理错误的情况
        LOG_DEBUG() << "QNetworkReply::finished";
        if (reply->error() != QNetworkReply::NoError) {
            LOG_WARN() << reply->errorString();
            // 发送信号通知完成
            emit this->SigHttpFinish(reqId, "", ErrorCodes::ERR_NETWORK);
        } else { // 无错误则读回请求
            QString res = reply->readAll();
            LOG_DEBUG() << res;
            // 发送信号通知完成
            emit this->SigHttpFinish(reqId, res, ErrorCodes::SUCCESS);
        }
        reply->deleteLater();
    });
}
