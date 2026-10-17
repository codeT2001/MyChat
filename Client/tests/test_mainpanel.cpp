#include <QtTest>

#include <QLineEdit>
#include <QListWidget>
#include <QStackedWidget>

#include "badgebutton.h"
#include "chatpage.h"
#include "chatsessionlist.h"
#include "domainmodels.h"
#include "mainpanel.h"
#include "usermanager.h"
#include "utils.h"

// 主面板界面交互：侧边栏切页、搜索覆盖页与「点击空白处退出搜索」、
// 点击会话条目进入聊天页。全部用 QTest 派发真实鼠标/键盘事件，
// 数据由内存中的 UserManager 预置，不连服务端。

class TestMainPanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sideButtonsSwitchPages();
    void typingInSearchShowsSearchPage();
    void clickingOutsideSearchListExitsSearch();
    void clickingSessionItemOpensChatPage();

private:
    static void showAndWait(QWidget &widget);
};

void TestMainPanel::initTestCase()
{
    Utils::ClearTips();

    auto &mgr = UserManager::GetInstance();
    mgr.SetUserInfo(std::make_shared<UserInfo>(9001, "self", "自己", "head_2", 1, "签名"));

    auto friendInfo = std::make_shared<FriendInfo>(2001, "alice", "小A", "head_1", 0, "hi");
    friendInfo->last_msg_ = "hello";
    friendInfo->AppendChatMsgs({std::make_shared<TextChatData>("m1", "hello", 2001, 9001)});
    mgr.AddFriend(friendInfo);
}

void TestMainPanel::showAndWait(QWidget &widget)
{
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
}

void TestMainPanel::sideButtonsSwitchPages()
{
    MainPanel panel;
    showAndWait(panel);

    auto *stack = panel.findChild<QStackedWidget *>("stackedWidget");
    auto *chatBtn = panel.findChild<BadgeButton *>("sideChatBtn");
    auto *contBtn = panel.findChild<BadgeButton *>("sideContBtn");
    QVERIFY(stack && chatBtn && contBtn);

    QCOMPARE(stack->currentWidget()->objectName(), QString("chatsPage")); // 默认聊天页

    QTest::mouseClick(contBtn, Qt::LeftButton);
    QCOMPARE(stack->currentWidget()->objectName(), QString("contactsPage"));
    QVERIFY(contBtn->isChecked());
    QVERIFY(!chatBtn->isChecked()); // 按钮组互斥

    QTest::mouseClick(chatBtn, Qt::LeftButton);
    QCOMPARE(stack->currentWidget()->objectName(), QString("chatsPage"));
    QVERIFY(chatBtn->isChecked());
}

void TestMainPanel::typingInSearchShowsSearchPage()
{
    MainPanel panel;
    showAndWait(panel);

    auto *stack = panel.findChild<QStackedWidget *>("stackedWidget");
    auto *searchEdit = panel.findChild<QLineEdit *>("searchEdit");
    QVERIFY(stack && searchEdit);

    QTest::keyClicks(searchEdit, "ali");
    QCOMPARE(searchEdit->text(), QString("ali"));
    QCOMPARE(stack->currentWidget()->objectName(), QString("searchPage")); // 有输入即覆盖为搜索页
}

void TestMainPanel::clickingOutsideSearchListExitsSearch()
{
    MainPanel panel;
    showAndWait(panel);

    auto *stack = panel.findChild<QStackedWidget *>("stackedWidget");
    auto *searchEdit = panel.findChild<QLineEdit *>("searchEdit");
    QVERIFY(stack && searchEdit);

    QTest::keyClicks(searchEdit, "ali");
    QCOMPARE(stack->currentWidget()->objectName(), QString("searchPage"));

    // 点侧边栏空白处（不在搜索结果列表内）→ 退出搜索、清空输入、回到上次的页面
    QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
    QCOMPARE(stack->currentWidget()->objectName(), QString("chatsPage"));
    QVERIFY(searchEdit->text().isEmpty());
}

void TestMainPanel::clickingSessionItemOpensChatPage()
{
    MainPanel panel;
    showAndWait(panel);

    auto *sessionList = panel.findChild<ChatSessionList *>("sessionList");
    auto *chatStack = panel.findChild<QStackedWidget *>("chatDataStackWidget");
    auto *chatPage = panel.findChild<ChatPage *>("chatPage");
    QVERIFY(sessionList && chatStack && chatPage);
    QVERIFY(sessionList->count() >= 1);

    QListWidgetItem *item = sessionList->item(0);
    const QRect itemRect = sessionList->visualItemRect(item);
    QTest::mouseClick(sessionList->viewport(), Qt::LeftButton, Qt::NoModifier, itemRect.center());

    QCOMPARE(chatStack->currentWidget()->objectName(), QString("chatPage"));
    QCOMPARE(chatPage->GetCurrentUid(), 2001);
}

QTEST_MAIN(TestMainPanel)
#include "test_mainpanel.moc"
