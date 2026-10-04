#ifndef RESETDIALOG_H
#define RESETDIALOG_H

#include "constants.h"
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
    void SlotResetModFinish(RequestId id, QString res, ErrorCodes err);
Q_SIGNALS:
    void SwitchLogin();

private:
    // Setup methods
    void SetupWindow();
    void SetupConnections();
    void SetupValidation();
    // Component methods
    void InitHttpHandles();
    bool CheckUserValid();
    bool CheckPasswordValid();
    bool CheckEmailValid();
    bool CheckVerifyCodeValid();
    Ui::ResetDialog *ui;
    QMap<RequestId, std::function<void(const QJsonObject &)>> handles_;
};

#endif // RESETDIALOG_H
