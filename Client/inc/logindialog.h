#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

class AuthService;

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(AuthService *authService, QWidget *parent = nullptr);
    ~LoginDialog();

public slots:
    void OnLoginBtnClicked();
    void OnLoginFailed();
    void OnLoginError(const QString &msg);

signals:
    void SwitchRegister();
    void SwitchReset();

private:
    void SetupWindow();
    void SetupConnections();
    void SetupPasswordToggle();
    void SetupValidation();
    bool CheckUserValid();
    bool CheckPasswordValid();

    Ui::LoginDialog *ui;
    AuthService *authService_;
};

#endif // LOGINDIALOG_H
