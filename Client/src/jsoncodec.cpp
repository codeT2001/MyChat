#include "jsoncodec.h"
#include "userdata.h"
#include <QJsonDocument>
#include <QJsonObject>

ErrorCodes ExtractError(const QJsonObject &obj)
{
    if (!obj.contains("error")) {
        return ErrorCodes::JSON_PARSE_ERROR;
    }
    return static_cast<ErrorCodes>(obj["error"].toInt());
}

std::shared_ptr<SearchInfo> JsonParser::ParseSearchRsp(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        return nullptr;
    }
    QJsonObject obj = doc.object();
    if (ExtractError(obj) != ErrorCodes::SUCCESS) {
        return nullptr;
    }
    return std::make_shared<SearchInfo>(obj["uid"].toInt(), obj["name"].toString(), obj["nick"].toString(),
                                        obj["desc"].toString(), obj["sex"].toInt(), obj["icon"].toString());
}

std::shared_ptr<AddFriendApply> JsonParser::ParseNotifyAddFriendReq(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        return nullptr;
    }
    QJsonObject obj = doc.object();
    if (ExtractError(obj) != ErrorCodes::SUCCESS) {
        return nullptr;
    }
    return std::make_shared<AddFriendApply>(obj["apply_uid"].toInt(), obj["name"].toString(), obj["desc"].toString(),
                                            obj["icon"].toString(), obj["nick"].toString(), obj["sex"].toInt());
}

NotifyAuthFriendResult JsonParser::ParseNotifyAuthFriendReq(const QByteArray &data)
{
    NotifyAuthFriendResult result;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        return result;
    }
    QJsonObject obj = doc.object();
    result.error = ExtractError(obj);
    if (result.error != ErrorCodes::SUCCESS) {
        return result;
    }
    result.action = obj.value("action").toInt(AuthAction::ACCEPT);
    if (result.action == AuthAction::ACCEPT) {
        result.info = std::make_shared<AuthPeerInfo>(obj["from_uid"].toInt(), obj["name"].toString(),
                                                     obj["nick"].toString(), obj["icon"].toString(),
                                                     obj["sex"].toInt());
    }
    return result;
}

AuthFriendRspResult JsonParser::ParseAuthFriendRsp(const QByteArray &data)
{
    AuthFriendRspResult result;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        return result;
    }
    QJsonObject obj = doc.object();
    result.error = ExtractError(obj);
    if (result.error != ErrorCodes::SUCCESS) {
        return result;
    }
    result.action = obj.value("action").toInt(AuthAction::ACCEPT);
    if (result.action == AuthAction::ACCEPT) {
        result.info = std::make_shared<AuthPeerInfo>(obj["uid"].toInt(), obj["name"].toString(),
                                                     obj["nick"].toString(), obj["icon"].toString(),
                                                     obj["sex"].toInt());
    }
    return result;
}

ErrorCodes JsonParser::ParseAddFriendRsp(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        return ErrorCodes::JSON_PARSE_ERROR;
    }
    return ExtractError(doc.object());
}

ErrorCodes JsonParser::ParseTextChatMsgRsp(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        return ErrorCodes::JSON_PARSE_ERROR;
    }
    return ExtractError(doc.object());
}

NotifyTextChatMsgResult JsonParser::ParseNotifyTextChatMsg(const QByteArray &data)
{
    NotifyTextChatMsgResult result;
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        return result;
    }
    QJsonObject obj = doc.object();
    result.error = ExtractError(obj);
    if (result.error != ErrorCodes::SUCCESS) {
        return result;
    }
    result.fromUid = obj["from_uid"].toInt();
    result.toUid = obj["to_uid"].toInt();
    QJsonArray arr = obj["text_array"].toArray();
    for (const QJsonValue &v : arr) {
        QJsonObject msgObj = v.toObject();
        QString msgid = msgObj["msg_id"].toString();
        QString content = msgObj["content"].toString();
        result.msgs.push_back(std::make_shared<TextChatData>(msgid, content, result.fromUid, result.toUid));
    }
    return result;
}

std::shared_ptr<UserInfo> JsonParser::ParseUserInfo(const QJsonObject &obj)
{
    return std::make_shared<UserInfo>(obj["uid"].toInt(), obj["name"].toString(), obj["nick"].toString(),
                                      obj["icon"].toString(), obj["sex"].toInt());
}

std::vector<std::shared_ptr<ApplyInfo>> JsonParser::ParseApplyList(const QJsonArray &array)
{
    std::vector<std::shared_ptr<ApplyInfo>> list;
    for (const QJsonValue &v : array) {
        auto name = v["name"].toString();
        auto uid = v["uid"].toInt();
        auto icon = v["icon"].toString();
        auto nick = v["nick"].toString();
        auto sex = v["sex"].toInt();
        auto desc = v["desc"].toString();
        auto status = v["status"].toInt();
        list.push_back(std::make_shared<ApplyInfo>(uid, name, desc, icon, nick, sex, status));
    }
    return list;
}

std::vector<std::shared_ptr<FriendInfo>> JsonParser::ParseFriendList(const QJsonArray &array)
{
    std::vector<std::shared_ptr<FriendInfo>> list;
    for (const QJsonValue &v : array) {
        auto name = v["name"].toString();
        auto desc = v["desc"].toString();
        auto icon = v["icon"].toString();
        auto nick = v["nick"].toString();
        auto sex = v["sex"].toInt();
        auto uid = v["uid"].toInt();
        list.push_back(std::make_shared<FriendInfo>(uid, name, nick, icon, sex, desc));
    }
    return list;
}

ServerInfo JsonParser::ParseLoginHttpRsp(const QJsonObject &obj)
{
    ServerInfo info;
    info.uid = obj["uid"].toInt();
    info.host = obj["host"].toString();
    info.port = obj["port"].toInt();
    info.token = obj["token"].toString();
    return info;
}

QByteArray JsonSerializer::SerializeChatLoginReq(int uid, const QString &token)
{
    QJsonObject jsonObj;
    jsonObj["uid"] = uid;
    jsonObj["token"] = token;
    return QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);
}

QByteArray JsonSerializer::SerializeSearchUserReq(const QString &uid)
{
    QJsonObject jsonObj;
    jsonObj["uid"] = uid;
    return QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);
}

QByteArray JsonSerializer::SerializeAddFriendReq(int fromUid,
                                                 int toUid,
                                                 const QString &name,
                                                 const QString &desc,
                                                 const QString &remarkName)
{
    QJsonObject jsonObj;
    jsonObj["from_uid"] = fromUid;
    jsonObj["to_uid"] = toUid;
    jsonObj["name"] = name;
    jsonObj["desc"] = desc;
    jsonObj["remark_name"] = remarkName;
    return QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);
}

QByteArray JsonSerializer::SerializeAuthFriendReq(int fromUid,
                                                  int toUid,
                                                  const QString &name,
                                                  const QString &desc,
                                                  const QString &remarkName,
                                                  int action)
{
    QJsonObject jsonObj;
    jsonObj["from_uid"] = fromUid;
    jsonObj["to_uid"] = toUid;
    jsonObj["name"] = name;
    jsonObj["desc"] = desc;
    jsonObj["remark_name"] = remarkName;
    jsonObj["action"] = action;
    return QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);
}

QByteArray JsonSerializer::SerializeTextChatMsgReq(int fromUid, int toUid, const QJsonArray &textArray)
{
    QJsonObject jsonObj;
    jsonObj["from_uid"] = fromUid;
    jsonObj["to_uid"] = toUid;
    jsonObj["text_array"] = textArray;
    return QJsonDocument(jsonObj).toJson(QJsonDocument::Compact);
}
