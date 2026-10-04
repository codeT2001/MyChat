#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include "constants.h"
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
    void SlotRegisterModFinish(RequestId id, QString res, ErrorCodes err);
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
    void InitHttpHandles();
    bool CheckUserValid();
    bool CheckPasswordValid();
    bool CheckConfirmValid();
    bool CheckEmailValid();
    bool CheckVerifyCodeValid();
    void ChangeTipPage();
    Ui::RegisterDialog *ui;
    QMap<RequestId, std::function<void(const QJsonObject &)>> handles_;
    QTimer *timer_;
    int32_t countDown_;
};

#endif // REGISTERDIALOG_H
