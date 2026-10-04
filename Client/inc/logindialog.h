#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

public slots:
    void OnLoginBtnClicked();
    void OnLoginFailed();
    void OnLoginError(const QString &msg);

signals:
    void SigSwitchRegister();
    void SigSwitchReset();

private:
    void SetupWindow();
    void SetupConnections();
    void SetupPasswordToggle();
    void SetupValidation();
    bool CheckUserValid();
    bool CheckPasswordValid();

    Ui::LoginDialog *ui;
};

#endif // LOGINDIALOG_H
