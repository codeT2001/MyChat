#ifndef USERMANAGER_H
#define USERMANAGER_H

#include <QObject>
#include "noncopyable.h"
#include <memory>
#include <vector>
#include <unordered_map>
struct ApplyInfo;
struct UserInfo;
struct AuthPeerInfo;
struct FriendInfo;
struct TextChatData;
class UserManager : public QObject {
    Q_OBJECT
public:
    static UserManager &GetInstance()
    {
        static UserManager instance;
        return instance;
    }
    ~UserManager();
    void SetUserInfo(std::shared_ptr<UserInfo> info);
    // 获取当前登录用户自己的资料
    std::shared_ptr<UserInfo> GetUserInfo() const;
    void SetToken(const QString &token);
    void AppendApplyList(const std::vector<std::shared_ptr<ApplyInfo>> &applyList);
    void AppendFriendList(const std::vector<std::shared_ptr<FriendInfo>> &friendList);
    int32_t GetUid() const;
    QString GetName() const;
    QString GetNick() const;
    QString GetIcon() const;
    QString GetDesc() const;
    QString GetToken() const;
    const std::unordered_map<int32_t, std::shared_ptr<ApplyInfo>> &GetApplyList();
    bool AlreadyApply(int32_t uid);
    void AddApply(std::shared_ptr<ApplyInfo> info);
    std::vector<std::shared_ptr<FriendInfo>> GetChatListPerPage();
    bool IsLoadChatFinish();
    void UpdateChatLoadedCount();
    std::vector<std::shared_ptr<FriendInfo>> GetContactListPerPage();
    void UpdateContactLoadedCount();
    bool IsLoadContactFinish();
    bool CheckFriendById(int uid);
    void AddFriend(std::shared_ptr<AuthPeerInfo> auth_info);
    void AddFriend(std::shared_ptr<FriendInfo> friend_info);
    std::shared_ptr<FriendInfo> GetFriendById(int32_t uid);
    // 更新好友备注名（本地数据，服务端同步协议待接入）
    void UpdateFriendRemark(int32_t uid, const QString &remark);
    // 更新好友标签（本地数据，服务端同步协议待接入）
    void UpdateFriendLabel(int32_t uid, const QString &label);
    void AppendFriendChatMsg(int friend_id, std::vector<std::shared_ptr<TextChatData>> msgs);

private:
    DISALLOW_COPY_MOVE(UserManager)
    UserManager();
    QString token_;
    std::shared_ptr<UserInfo> userInfo_;
    std::unordered_map<int32_t, std::shared_ptr<ApplyInfo>> applyList_;
    std::unordered_map<int32_t, std::shared_ptr<FriendInfo>> friendMap_;
    int chatLoaded_;
    int contactLoaded_;
public slots:
    void SlotFriendAuth(std::shared_ptr<FriendInfo> info);
};

#endif // USERMANAGER_H
