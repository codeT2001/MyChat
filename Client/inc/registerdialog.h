#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QDialog>
#include <QTimer>
namespace Ui {
class RegisterDialog;
}

class RegisterDialog : public QDialog {
    Q_OBJECT

public:
    explicit RegisterDialog(QWidget *parent = nullptr);
    ~RegisterDialog();
public Q_SLOTS:
    void OnGetCodeClicked();
    void OnSureBtnClicked();
    void OnCancelClicked();
    void OnBackToLoginClicked();
    void OnTimerTimeout();
    // AuthService 结果回调（UI 只展示结果，不感知网络细节）
    void OnVerifyCodeResult(bool ok, const QString &msg);
    void OnRegisterResult(bool ok, const QString &msg);
Q_SIGNALS:
    void SwitchLogin();

private:
    // Setup methods
    void SetupWindow();
    void SetupConnections();
    void SetupPasswordToggle();
    void SetupValidation();
    void SetupTimer();
    // Component methods
    bool CheckUserValid();
    bool CheckPasswordValid();
    bool CheckConfirmValid();
    bool CheckEmailValid();
    bool CheckVerifyCodeValid();
    void ChangeTipPage();
    Ui::RegisterDialog *ui;
    QTimer *timer_;
    int32_t countDown_;
};

#endif // REGISTERDIALOG_H
