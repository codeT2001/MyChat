#ifndef RESETDIALOG_H
#define RESETDIALOG_H

#include <QDialog>

namespace Ui {
class ResetDialog;
}

class ResetDialog : public QDialog {
    Q_OBJECT

public:
    explicit ResetDialog(QWidget *parent = nullptr);
    ~ResetDialog();
public Q_SLOTS:
    void OnGetCodeClicked();
    void OnSureBtnClicked();
    // AuthService 结果回调（UI 只展示结果，不感知网络细节）
    void OnVerifyCodeResult(bool ok, const QString &msg);
    void OnResetResult(bool ok, const QString &msg);
Q_SIGNALS:
    void SigSwitchLogin();

private:
    // Setup methods
    void SetupWindow();
    void SetupConnections();
    void SetupValidation();
    // Component methods
    bool CheckUserValid();
    bool CheckPasswordValid();
    bool CheckEmailValid();
    bool CheckVerifyCodeValid();
    Ui::ResetDialog *ui;
};

#endif // RESETDIALOG_H
