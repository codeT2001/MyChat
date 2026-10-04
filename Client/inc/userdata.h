#ifndef USERDATA_H
#define USERDATA_H
#include <QString>
#include <memory>
#include <vector>

// ============================================================
// 数据模型分层：
//   UserBase       协议通用的用户资料五件套（uid/name/nick/icon/sex）
//     ├ SearchInfo      搜索用户结果（+签名 desc）
//     ├ AddFriendApply  收到的加好友申请通知（+留言 desc）
//     │  └ ApplyInfo    申请列表项（+处理状态 status）
//     ├ AuthPeerInfo    好友认证对端信息（通知/响应统一）
//     ├ FriendInfo      好友聚合实体（+签名/最后消息/消息记录）
//     └ UserInfo        当前登录用户自己的资料（+签名 desc）
//   TextChatData   单条聊天消息
// ============================================================

// 用户资料基类：各协议消息中重复出现的通用字段
struct UserBase {
    UserBase() = default;
    UserBase(int uid, QString name, QString nick, QString icon, int sex)
        : uid_(uid),
          name_(std::move(name)),
          nick_(std::move(nick)),
          icon_(std::move(icon)),
          sex_(sex)
    {
    }

    int uid_ = 0;
    QString name_; // 用户名（列表显示名/备注名）
    QString nick_; // 昵称
    QString icon_;
    int sex_ = 0; // 协议字段，当前 UI 未消费
};

// 搜索用户响应
struct SearchInfo : UserBase {
    SearchInfo(int uid, const QString &name, const QString &nick, const QString &desc, int sex,
               const QString &icon = "")
        : UserBase(uid, name, nick, icon, sex), desc_(desc)
    {
    }

    QString desc_; // 个性签名
};

// 收到的加好友申请通知（apply_uid 即申请人 uid，对应基类 uid_）
struct AddFriendApply : UserBase {
    AddFriendApply(int apply_uid,
                   const QString &name,
                   const QString &desc,
                   const QString &icon,
                   const QString &nick,
                   int sex)
        : UserBase(apply_uid, name, nick, icon, sex), desc_(desc)
    {
    }

    QString desc_; // 申请留言
};

// 申请列表项：申请内容 + 处理状态（0=待处理，非 0=已处理）
struct ApplyInfo : AddFriendApply {
    ApplyInfo(int uid,
              const QString &name,
              const QString &desc,
              const QString &icon,
              const QString &nick,
              int sex,
              int status)
        : AddFriendApply(uid, name, desc, icon, nick, sex), status_(status)
    {
    }

    ApplyInfo(const std::shared_ptr<AddFriendApply> &apply)
        : AddFriendApply(apply->uid_, apply->name_, apply->desc_, apply->icon_, apply->nick_, apply->sex_),
          status_(0)
    {
    }

    void SetIcon(const QString &head)
    {
        icon_ = head;
    }

    int status_ = 0;
};

// 好友认证对端信息：NOTIFY_AUTH_FRIEND_REQ 与 AUTH_FRIEND_RSP 结构一致，统一使用
struct AuthPeerInfo : UserBase {
    AuthPeerInfo(int uid, const QString &name, const QString &nick, const QString &icon, int sex)
        : UserBase(uid, name, nick, icon, sex)
    {
    }
};

struct TextChatData;

// 好友聚合实体：UserManager 好友表中的核心业务对象
struct FriendInfo : UserBase {
    FriendInfo(int uid,
               const QString &name,
               const QString &nick,
               const QString &icon,
               int sex,
               const QString &desc,
               const QString &last_msg = "")
        : UserBase(uid, name, nick, icon, sex), desc_(desc), last_msg_(last_msg)
    {
    }

    FriendInfo(const std::shared_ptr<AuthPeerInfo> &auth)
        : UserBase(auth->uid_, auth->name_, auth->nick_, auth->icon_, auth->sex_)
    {
    }

    void AppendChatMsgs(const std::vector<std::shared_ptr<TextChatData>> &text_vec);

    QString desc_; // 个性签名
    QString label_; // 朋友标签（本地编辑，服务端同步协议待接入）
    QString last_msg_; // 最近一条消息（会话列表预览）
    std::vector<std::shared_ptr<TextChatData>> chat_msgs_; // 历史消息
};

// 当前登录用户自己的资料（仅资料字段，不夹带会话/消息数据）
struct UserInfo : UserBase {
    UserInfo(int uid,
             const QString &name,
             const QString &nick,
             const QString &icon,
             int sex,
             const QString &desc = "")
        : UserBase(uid, name, nick, icon, sex), desc_(desc)
    {
    }

    // 仅供 UI 临时构造（uid 未知时）
    UserInfo(int uid, const QString &name, const QString &icon)
        : UserBase(uid, name, name, icon, 0)
    {
    }

    UserInfo(const std::shared_ptr<AuthPeerInfo> &auth)
        : UserBase(auth->uid_, auth->name_, auth->nick_, auth->icon_, auth->sex_)
    {
    }

    QString desc_; // 个性签名
};

// 单条聊天消息
struct TextChatData {
    TextChatData(const QString &msg_id, const QString &msg_content, int fromuid, int touid)
        : msg_id_(msg_id), msg_content_(msg_content), from_uid_(fromuid), to_uid_(touid)
    {
    }

    QString msg_id_;
    QString msg_content_;
    int from_uid_;
    int to_uid_;
};

#endif // USERDATA_H
