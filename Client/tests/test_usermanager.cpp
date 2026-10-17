#include <QtTest>

#include "constants.h"
#include "domainmodels.h"
#include "usermanager.h"

// 数据管理层（UserManager 单例）：申请列表、好友表、分页游标、备注/标签、消息追加。
// 用例按声明顺序执行（Qt Test 默认行为）：先把「登录前的空状态」验掉，再写入数据。

namespace {

std::shared_ptr<FriendInfo> MakeFriend(int uid, const QString &name)
{
    return std::make_shared<FriendInfo>(uid, name, QString("nick%1").arg(uid), "head_1", 0, "desc");
}

std::shared_ptr<TextChatData> MakeMsg(const QString &id, const QString &content, int from, int to)
{
    return std::make_shared<TextChatData>(id, content, from, to);
}

} // namespace

class TestUserManager : public QObject
{
    Q_OBJECT

private slots:
    void emptyStateBeforeLogin();
    void userInfoAndTokenRoundTrip();
    void applyListKeepsFirstAndIsIdempotent();
    void friendAddFromPeerInfo();
    void friendAddRejectsNullPeerInfo();
    void friendPagingAdvancesCursor();
    void friendPagingExactPageBoundary();
    void remarkAndLabelOnlyTouchExistingFriend();
    void appendChatMsgsKeepsOrder();
};

void TestUserManager::emptyStateBeforeLogin()
{
    UserManager &mgr = UserManager::GetInstance();
    QVERIFY(mgr.GetUserInfo() == nullptr);
    QCOMPARE(mgr.GetUid(), -1);
    QCOMPARE(mgr.GetName(), QString("undefined"));
    QVERIFY(mgr.GetToken().isEmpty());
    QVERIFY(mgr.GetApplyList().empty());
    QVERIFY(!mgr.CheckFriendById(1));
    QVERIFY(mgr.GetFriendById(1) == nullptr);
    QVERIFY(mgr.IsFriendListExhausted(0)); // 空好友表视为已加载完
}

void TestUserManager::userInfoAndTokenRoundTrip()
{
    UserManager &mgr = UserManager::GetInstance();
    mgr.SetUserInfo(std::make_shared<UserInfo>(9001, "self", "自己", "head_2", 1, "签名"));
    mgr.SetToken("tok-self");

    QCOMPARE(mgr.GetUid(), 9001);
    QCOMPARE(mgr.GetName(), QString("self"));
    QCOMPARE(mgr.GetNick(), QString("自己"));
    QCOMPARE(mgr.GetIcon(), QString("head_2"));
    QCOMPARE(mgr.GetDesc(), QString("签名"));
    QCOMPARE(mgr.GetToken(), QString("tok-self"));
}

void TestUserManager::applyListKeepsFirstAndIsIdempotent()
{
    UserManager &mgr = UserManager::GetInstance();
    auto first = std::make_shared<ApplyInfo>(100, "alice", "想加你", "head_1", "小A", 1, 0);
    mgr.AddApply(first);
    QVERIFY(mgr.AlreadyApply(100));
    QCOMPARE(static_cast<int>(mgr.GetApplyList().size()), 1);

    // 同一申请人重复到达时保留首次记录（不覆盖状态/留言）
    auto again = std::make_shared<ApplyInfo>(100, "alice2", "重复申请", "head_3", "小A2", 2, 1);
    mgr.AddApply(again);
    QCOMPARE(static_cast<int>(mgr.GetApplyList().size()), 1);
    QCOMPARE(mgr.GetApplyList().at(100)->name_, QString("alice"));
    QCOMPARE(mgr.GetApplyList().at(100)->desc_, QString("想加你"));

    // 一次性批量写入：uid 为键
    std::vector<std::shared_ptr<ApplyInfo>> batch = {
        std::make_shared<ApplyInfo>(101, "bob", "d", "head_1", "n", 0, 0),
        std::make_shared<ApplyInfo>(102, "carol", "d", "head_1", "n", 0, 0),
        std::make_shared<ApplyInfo>(100, "dup", "d", "head_1", "n", 0, 0), // 覆盖既有
    };
    mgr.AppendApplyList(batch);
    QCOMPARE(static_cast<int>(mgr.GetApplyList().size()), 3);
    QCOMPARE(mgr.GetApplyList().at(100)->name_, QString("dup")); // 批量写入按覆盖语义
}

void TestUserManager::friendAddFromPeerInfo()
{
    UserManager &mgr = UserManager::GetInstance();
    auto peer = std::make_shared<AcceptPeerInfo>(200, "dave", "小D", "head_4", 1);
    mgr.AddFriend(peer);

    QVERIFY(mgr.CheckFriendById(200));
    auto friendInfo = mgr.GetFriendById(200);
    QVERIFY(friendInfo != nullptr);
    QCOMPARE(friendInfo->name_, QString("dave"));
    QCOMPARE(friendInfo->nick_, QString("小D"));
    QCOMPARE(friendInfo->icon_, QString("head_4"));
    QCOMPARE(friendInfo->sex_, 1);
    QVERIFY(friendInfo->chat_msgs_.empty()); // 新建好友无历史消息
}

void TestUserManager::friendAddRejectsNullPeerInfo()
{
    UserManager &mgr = UserManager::GetInstance();
    const int before = static_cast<int>(mgr.GetFriendById(200)->uid_); // 前置数据仍在
    QCOMPARE(before, 200);

    // 空指针不得崩溃（服务端回包缺资料时不能把进程带走）
    mgr.AddFriend(std::shared_ptr<AcceptPeerInfo>());
    mgr.AddFriend(std::shared_ptr<FriendInfo>());
    QCOMPARE(static_cast<int>(mgr.GetFriendById(200)->uid_), 200);
}

void TestUserManager::friendPagingAdvancesCursor()
{
    UserManager &mgr = UserManager::GetInstance();
    for (int uid = 300; uid < 300 + 15; ++uid) {
        mgr.AddFriend(MakeFriend(uid, QString("f%1").arg(uid)));
    }
    // 好友表里已有 1 个前置好友（uid=200）
    const int total = 16;
    QVERIFY(!mgr.IsFriendListExhausted(0));

    int cursor = 0;
    const auto page1 = mgr.GetFriendsPerPage(cursor);
    QCOMPARE(static_cast<int>(page1.size()), 13); // 每页 13
    QCOMPARE(cursor, 13);

    const auto page2 = mgr.GetFriendsPerPage(cursor);
    QCOMPARE(static_cast<int>(page2.size()), total - 13);
    QCOMPARE(cursor, total);
    QVERIFY(mgr.IsFriendListExhausted(cursor));

    // 越界再取返回空且游标不变
    const auto page3 = mgr.GetFriendsPerPage(cursor);
    QVERIFY(page3.empty());
    QCOMPARE(cursor, total);

    // 两个调用方各自维护游标（会话列表 / 通讯录互不影响）
    int otherCursor = 0;
    const auto otherPage = mgr.GetFriendsPerPage(otherCursor);
    QCOMPARE(static_cast<int>(otherPage.size()), 13);
    QCOMPARE(otherCursor, 13);
}

void TestUserManager::friendPagingExactPageBoundary()
{
    UserManager &mgr = UserManager::GetInstance();
    // 此时好友表 = 1（uid 200，来自 friendAddFromPeerInfo）+ 15（300..314）= 16
    // 页大小 13：翻完两页正好 13 + 3，累计不重不漏、游标停在总数上
    int cursor = 0;
    int loaded = 0;
    while (!mgr.IsFriendListExhausted(cursor)) {
        loaded += static_cast<int>(mgr.GetFriendsPerPage(cursor).size());
    }
    QCOMPARE(loaded, 16);
    QCOMPARE(cursor, 16);
}

void TestUserManager::remarkAndLabelOnlyTouchExistingFriend()
{
    UserManager &mgr = UserManager::GetInstance();
    mgr.UpdateFriendRemark(200, "备注名");
    mgr.UpdateFriendLabel(200, "同事");
    QCOMPARE(mgr.GetFriendById(200)->name_, QString("备注名"));
    QCOMPARE(mgr.GetFriendById(200)->label_, QString("同事"));

    // 不存在的 uid：只记日志，不新增、不崩溃
    const int before = static_cast<int>(mgr.GetFriendById(200)->name_.size());
    mgr.UpdateFriendRemark(99999, "x");
    mgr.UpdateFriendLabel(99999, "x");
    QCOMPARE(static_cast<int>(mgr.GetFriendById(200)->name_.size()), before);
    QVERIFY(mgr.GetFriendById(99999) == nullptr);
}

void TestUserManager::appendChatMsgsKeepsOrder()
{
    UserManager &mgr = UserManager::GetInstance();
    mgr.AppendFriendChatMsg(200, {MakeMsg("m1", "第一条", 9001, 200), MakeMsg("m2", "第二条", 200, 9001)});
    mgr.AppendFriendChatMsg(200, {MakeMsg("m3", "第三条", 9001, 200)});

    const auto friendInfo = mgr.GetFriendById(200);
    QCOMPARE(static_cast<int>(friendInfo->chat_msgs_.size()), 3);
    QCOMPARE(friendInfo->chat_msgs_[0]->msg_id_, QString("m1"));
    QCOMPARE(friendInfo->chat_msgs_[1]->msg_content_, QString("第二条"));
    QCOMPARE(friendInfo->chat_msgs_[2]->msg_id_, QString("m3"));

    // 未知好友：只记日志，不崩溃
    mgr.AppendFriendChatMsg(99999, {MakeMsg("mx", "x", 1, 99999)});
    QCOMPARE(static_cast<int>(friendInfo->chat_msgs_.size()), 3);
}

QTEST_GUILESS_MAIN(TestUserManager)
#include "test_usermanager.moc"
