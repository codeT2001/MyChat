#ifndef FRIENDSERVICE_H
#define FRIENDSERVICE_H

#include <QObject>
#include <QByteArray>
#include <memory>
#include "constants.h"

struct SearchInfo;
struct AddFriendApply;
struct FriendInfo;

// 好友业务服务层：承接搜索用户、添加好友、好友认证等业务。
// UI 只通过本服务与网络交互，不直接调用 TcpManager 单例。
class FriendService : public QObject {
    Q_OBJECT
public:
    static FriendService &GetInstance()
    {
        static FriendService instance;
        return instance;
    }

    // 搜索用户（按 uid）
    void SearchUser(const QString &uid);
    // 发起加好友请求
    void AddFriend(int fromUid, int toUid, const QString &name, const QString &desc, const QString &remarkName);
    // 发起好友认证（同意添加）
    void AcceptFriend(int fromUid, int toUid, const QString &name, const QString &desc, const QString &remarkName);
    // 拒绝好友请求（fromUid=申请人, toUid=当前拒绝者）
    void RejectFriend(int fromUid, int toUid);

signals:
    void SigUserSearch(std::shared_ptr<SearchInfo> info);
    void SigFriendApply(std::shared_ptr<AddFriendApply> info);
    void SigFriendAccepted(std::shared_ptr<FriendInfo> info);
    // 我方申请被对方拒绝
    void SigFriendRejected();

private slots:
    void OnMessageReceived(RequestId id, const QByteArray &data);

private:
    FriendService();
    ~FriendService() = default;
    FriendService(const FriendService &) = delete;
    FriendService &operator=(const FriendService &) = delete;
};

#endif // FRIENDSERVICE_H
