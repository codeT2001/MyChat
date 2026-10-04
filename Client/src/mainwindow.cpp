#include "mainwindow.h"
#include "logindialog.h"
#include "authservice.h"
#include "./ui_mainwindow.h"
#include <qdebug.h>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    loginDialog_ = new LoginDialog(this);
    connect(loginDialog_, &LoginDialog::SwitchRegister, this, &MainWindow::SwitchRegisterDialog);
    connect(loginDialog_, &LoginDialog::SwitchReset, this, &MainWindow::SwitchResetDialog);

    // MainWindow 通过 AuthService 监听登录成功（不再直接依赖 HttpManager/TcpManager）
    connect(&AuthService::GetInstance(), &AuthService::sigLoginSuccess, this, &MainWindow::SlotLoginSuccess);

    setCentralWidget(loginDialog_);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::SwitchRegisterDialog()
{
    if (loginDialog_) {
        loginDialog_->deleteLater();
        loginDialog_ = nullptr;
    }

    if (!registerDialog_) {
        registerDialog_ = new RegisterDialog(this);
        connect(registerDialog_, &RegisterDialog::SwitchLogin, this, &MainWindow::SwitchLoginDialog);
    }

    setCentralWidget(registerDialog_);
}

void MainWindow::SwitchLoginDialog()
{
    if (registerDialog_) {
        registerDialog_->deleteLater();
        registerDialog_ = nullptr;
    }
    if (resetDialog_) {
        resetDialog_->deleteLater();
        resetDialog_ = nullptr;
    }
    if (!loginDialog_) {
        loginDialog_ = new LoginDialog(this);
        connect(loginDialog_, &LoginDialog::SwitchRegister, this, &MainWindow::SwitchRegisterDialog);
        connect(loginDialog_, &LoginDialog::SwitchReset, this, &MainWindow::SwitchResetDialog);
    }
    setCentralWidget(loginDialog_);
}

void MainWindow::SwitchResetDialog()
{
    if (loginDialog_) {
        loginDialog_->deleteLater();
        loginDialog_ = nullptr;
    }

    if (!resetDialog_) {
        resetDialog_ = new ResetDialog(this);
        connect(resetDialog_, &ResetDialog::SwitchLogin, this, &MainWindow::SwitchLoginDialog);
    }

    setCentralWidget(resetDialog_);
}

void MainWindow::SlotLoginSuccess()
{
    if (loginDialog_) {
        loginDialog_->deleteLater();
        loginDialog_ = nullptr;
    }
    if (!chatWindow_) {
        chatWindow_ = new ChatWindow(this);
    }
    this->setMinimumSize(QSize(1050, 900));
    this->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    setCentralWidget(chatWindow_);
}
