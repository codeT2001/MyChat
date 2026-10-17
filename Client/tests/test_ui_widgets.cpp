#include <QtTest>

#include <QLineEdit>
#include <QLabel>
#include <QPushButton>

#include "authservice.h"
#include "countdownbutton.h"
#include "logindialog.h"
#include "messagetextedit.h"
#include "statefulclicklabel.h"
#include "utils.h"

// 界面交互测试：进程内用 QTest 直接派发鼠标/键盘事件（等价于「手工点界面」），
// 使用 offscreen 平台，无需显示器与服务端。
//
// 覆盖范围：登录框表单校验与提示、密码可见性、页面切换信号、错误回显、
// 验证码倒计时按钮、输入框回车发送、通用可点击标签。
// 不覆盖：真实网络往返与聊天主界面整链路（需要服务端联调，见 Client/README.md）。

class TestUiWidgets : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void loginRejectsEmptyInput();
    void loginRejectsBadUserName();
    void loginRejectsShortPassword();
    void loginValidationClearsWhenInputBecomesValid();
    void loginPasswordToggleSwitchesEchoMode();
    void loginSwitchButtonsEmitSignals();
    void loginErrorSlotShowsMessageAndReenablesButton();

    void countdownButtonDisablesAndCountsDown();
    void messageEditEnterEmitsSend();
    void statefulLabelTogglesAndEmitsClicked();

private:
    static void showAndWait(QWidget &widget);
};

void TestUiWidgets::init()
{
    Utils::ClearTips(); // 提示文案是进程级静态栈，用例之间必须清空
}

void TestUiWidgets::showAndWait(QWidget &widget)
{
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
}

void TestUiWidgets::loginRejectsEmptyInput()
{
    LoginDialog dialog;
    showAndWait(dialog);

    auto *loginBtn = dialog.findChild<QPushButton *>("loginBtn");
    auto *tip = dialog.findChild<QLabel *>("loginErrTip");
    QVERIFY2(loginBtn != nullptr, "loginBtn not found");
    QVERIFY2(tip != nullptr, "loginErrTip not found");

    QTest::mouseClick(loginBtn, Qt::LeftButton);

    QCOMPARE(tip->text(), QString("用户名长度应为3~20字符"));
    QVERIFY(loginBtn->isEnabled()); // 校验失败不应进入登录中状态
}

void TestUiWidgets::loginRejectsBadUserName()
{
    LoginDialog dialog;
    showAndWait(dialog);

    auto *userName = dialog.findChild<QLineEdit *>("userName");
    auto *password = dialog.findChild<QLineEdit *>("password");
    auto *loginBtn = dialog.findChild<QPushButton *>("loginBtn");
    auto *tip = dialog.findChild<QLabel *>("loginErrTip");
    QVERIFY(userName && password && loginBtn && tip);

    userName->setText("ab-cd"); // 长度合法、字符非法
    password->setText("abc123");
    QTest::mouseClick(loginBtn, Qt::LeftButton);

    QCOMPARE(tip->text(), QString("用户名只能包含字母、数字和下划线_"));
    QVERIFY(loginBtn->isEnabled());
}

void TestUiWidgets::loginRejectsShortPassword()
{
    LoginDialog dialog;
    showAndWait(dialog);

    auto *userName = dialog.findChild<QLineEdit *>("userName");
    auto *password = dialog.findChild<QLineEdit *>("password");
    auto *loginBtn = dialog.findChild<QPushButton *>("loginBtn");
    auto *tip = dialog.findChild<QLabel *>("loginErrTip");
    QVERIFY(userName && password && loginBtn && tip);

    userName->setText("alice");
    password->setText("123");
    QTest::mouseClick(loginBtn, Qt::LeftButton);

    QCOMPARE(tip->text(), QString("密码长度应为6~15字符"));
    QVERIFY(loginBtn->isEnabled());
}

void TestUiWidgets::loginValidationClearsWhenInputBecomesValid()
{
    LoginDialog dialog;
    showAndWait(dialog);

    auto *userName = dialog.findChild<QLineEdit *>("userName");
    auto *tip = dialog.findChild<QLabel *>("loginErrTip");
    QVERIFY(userName && tip);

    // 输错后失焦：提示出现
    userName->setText("ab-cd");
    userName->setFocus();
    QTest::keyClick(userName, Qt::Key_Tab);
    QVERIFY(!tip->text().isEmpty());

    // 改成合法用户名后再次失焦：提示清除
    userName->setText("alice_01");
    userName->setFocus();
    QTest::keyClick(userName, Qt::Key_Tab);
    QVERIFY2(tip->text().isEmpty(), qPrintable(QString("tip should be cleared, got: ") + tip->text()));
}

void TestUiWidgets::loginPasswordToggleSwitchesEchoMode()
{
    LoginDialog dialog;
    showAndWait(dialog);

    auto *password = dialog.findChild<QLineEdit *>("password");
    auto *toggle = dialog.findChild<StatefulClickLabel *>("loginPassLb");
    QVERIFY(password && toggle);

    QCOMPARE(password->echoMode(), QLineEdit::Password);
    QTest::mouseClick(toggle, Qt::LeftButton);
    QCOMPARE(toggle->GetCurState(), ClickLabelState::SELECTED);
    QCOMPARE(password->echoMode(), QLineEdit::Normal); // 第一下：显示明文

    QTest::mouseClick(toggle, Qt::LeftButton);
    QCOMPARE(toggle->GetCurState(), ClickLabelState::NORMAL);
    QCOMPARE(password->echoMode(), QLineEdit::Password); // 第二下：恢复掩码
}

void TestUiWidgets::loginSwitchButtonsEmitSignals()
{
    LoginDialog dialog;
    showAndWait(dialog);

    auto *registerBtn = dialog.findChild<QPushButton *>("registerBtn");
    auto *forgetLabel = dialog.findChild<StatefulClickLabel *>("forgetLabel");
    QVERIFY(registerBtn && forgetLabel);

    QSignalSpy switchRegister(&dialog, &LoginDialog::SigSwitchRegister);
    QSignalSpy switchReset(&dialog, &LoginDialog::SigSwitchReset);

    QTest::mouseClick(registerBtn, Qt::LeftButton);
    QCOMPARE(switchRegister.count(), 1);
    QCOMPARE(switchReset.count(), 0);

    QTest::mouseClick(forgetLabel, Qt::LeftButton);
    QCOMPARE(switchReset.count(), 1);
}

void TestUiWidgets::loginErrorSlotShowsMessageAndReenablesButton()
{
    LoginDialog dialog;
    showAndWait(dialog);

    auto *loginBtn = dialog.findChild<QPushButton *>("loginBtn");
    auto *tip = dialog.findChild<QLabel *>("loginErrTip");
    QVERIFY(loginBtn && tip);

    // 模拟服务端回包（避免真的发 HTTP）：登录按钮先进入「登录中」，随后错误回显并恢复可点
    loginBtn->setEnabled(false);
    dialog.OnLoginError(QString("姓名与邮箱不匹配"));

    QCOMPARE(tip->text(), QString("姓名与邮箱不匹配"));
    QVERIFY(loginBtn->isEnabled());
}

void TestUiWidgets::countdownButtonDisablesAndCountsDown()
{
    CountdownButton button;
    button.setText(QString("获取"));
    showAndWait(button);

    QSignalSpy clicked(&button, &QPushButton::clicked);
    QTest::mouseClick(&button, Qt::LeftButton);

    QCOMPARE(clicked.count(), 1);
    QVERIFY(!button.isEnabled());
    QCOMPARE(button.text(), QString("60s"));

    QTRY_COMPARE_WITH_TIMEOUT(button.text(), QString("59s"), 2500); // 1 秒后自减
    QVERIFY(!button.isEnabled()); // 倒计时期间保持禁用
}

void TestUiWidgets::messageEditEnterEmitsSend()
{
    MessageTextEdit edit;
    showAndWait(edit);

    QSignalSpy send(&edit, &MessageTextEdit::SigSend);
    edit.setPlainText(QString("hello"));

    QTest::keyClick(&edit, Qt::Key_Return);
    QCOMPARE(send.count(), 1);

    // Shift+Enter 是换行，不应触发发送
    QTest::keyClick(&edit, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(send.count(), 1);
}

void TestUiWidgets::statefulLabelTogglesAndEmitsClicked()
{
    StatefulClickLabel label(QString("toggle"));
    label.SetStateStyles("normal", "hover", "", "selected", "selectedHover", "");
    showAndWait(label);

    QSignalSpy clicked(&label, &StatefulClickLabel::clicked);
    QCOMPARE(label.GetCurState(), ClickLabelState::NORMAL);

    QTest::mouseClick(&label, Qt::LeftButton);
    QCOMPARE(clicked.count(), 1);
    QCOMPARE(label.GetCurState(), ClickLabelState::SELECTED);

    QTest::mouseClick(&label, Qt::LeftButton);
    QCOMPARE(clicked.count(), 2);
    QCOMPARE(label.GetCurState(), ClickLabelState::NORMAL);
}

QTEST_MAIN(TestUiWidgets)
#include "test_ui_widgets.moc"
