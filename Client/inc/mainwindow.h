#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "registerdialog.h"
#include "resetdialog.h"
#include "mainpanel.h"

class LoginDialog;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public Q_SLOTS:
    void OnSwitchRegisterDialog();
    void OnSwitchLoginDialog();
    void OnSwitchResetDialog();
    void OnLoginSuccess();
    void OnKicked(const QString &msg);

private:
    Ui::MainWindow *ui;
    LoginDialog *loginDialog_ = nullptr;
    RegisterDialog *registerDialog_ = nullptr;
    ResetDialog *resetDialog_ = nullptr;
    MainPanel *mainPanel_ = nullptr;
};
#endif // MAINWINDOW_H
