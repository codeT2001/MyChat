#include "friendservice.h"
#include "tcpmanager.h"
#include "jsoncodec.h"
#include "usermanager.h"
#include "userdata.h"
#include "log.h"

FriendService::FriendService()
{
    connect(&TcpManager::GetInstance(), &TcpManager::SigMessageReceived, this, &FriendService::OnMessageReceived);
}

void FriendService::SearchUser(const QString &uid)
{
    QByteArray data = JsonSerializer::SerializeSearchUserReq(uid);
    TcpManager::GetInstance().SlotSendData(RequestId::SEARCH_USER_REQ, data);
}

void FriendService::AddFriend(int fromUid,
                              int toUid,
                              const QString &name,
                              const QString &desc,
                              const QString &remarkName)
{
    QByteArray data = JsonSerializer::SerializeAddFriendReq(fromUid, toUid, name, desc, remarkName);
    TcpManager::GetInstance().SlotSendData(RequestId::ADD_FRIEND_REQ, data);
}

void FriendService::AuthFriend(int fromUid,
                               int toUid,
                               const QString &name,
                               const QString &desc,
                               const QString &remarkName)
{
    QByteArray data =
        JsonSerializer::SerializeAuthFriendReq(fromUid, toUid, name, desc, remarkName, AuthAction::ACCEPT);
    TcpManager::GetInstance().SlotSendData(RequestId::AUTH_FRIEND_REQ, data);
}

void FriendService::RejectFriend(int fromUid, int toUid)
{
    // 复用 AUTH_FRIEND_REQ，action=0 表示拒绝
    QByteArray data =
        JsonSerializer::SerializeAuthFriendReq(fromUid, toUid, QString(), QString(), QString(), AuthAction::REJECT);
    TcpManager::GetInstance().SlotSendData(RequestId::AUTH_FRIEND_REQ, data);
}

void FriendService::OnMessageReceived(RequestId id, const QByteArray &data)
{
    switch (id) {
        case RequestId::SEARCH_USER_RSP: {
            auto info = JsonParser::ParseSearchRsp(data);
            emit SigUserSearch(info);
            break;
        }
        case RequestId::NOTIFY_ADD_FRIEND_REQ: {
            auto apply = JsonParser::ParseNotifyAddFriendReq(data);
            if (apply) {
                emit SigFriendApply(apply);
            }
            break;
        }
        case RequestId::NOTIFY_AUTH_FRIEND_REQ: {
            auto result = JsonParser::ParseNotifyAuthFriendReq(data);
            if (result.error != ErrorCodes::SUCCESS) {
                break;
            }
            if (result.action == AuthAction::ACCEPT && result.info) {
                // 对方同意了我方申请
                auto friendInfo = std::make_shared<FriendInfo>(result.info);
                UserManager::GetInstance().AddFriend(friendInfo);
                emit SigFriendAuth(friendInfo);
            } else {
                // 对方拒绝了我方申请
                emit SigFriendRejected(nullptr);
            }
            break;
        }
        case RequestId::AUTH_FRIEND_RSP: {
            auto result = JsonParser::ParseAuthFriendRsp(data);
            if (result.error != ErrorCodes::SUCCESS) {
                LOG_WARN() << "AuthFriend rsp error:" << static_cast<int>(result.error);
                break;
            }
            if (result.action == AuthAction::ACCEPT && result.info) {
                // 我方同意，服务端确认成功
                auto friendInfo = std::make_shared<FriendInfo>(result.info);
                UserManager::GetInstance().AddFriend(friendInfo);
                emit SigFriendAuth(friendInfo);
            }
            // action==REJECT 时服务端确认拒绝成功，无需额外处理（本地已乐观更新）
            break;
        }
        case RequestId::ADD_FRIEND_RSP: {
            ErrorCodes err = JsonParser::ParseAddFriendRsp(data);
            if (err != ErrorCodes::SUCCESS) {
                LOG_WARN() << "AddFriend rsp error:" << static_cast<int>(err);
            }
            break;
        }
        default:
            break;
    }
}
