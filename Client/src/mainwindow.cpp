#include "mainwindow.h"
#include "logindialog.h"
#include "authservice.h"
#include "./ui_mainwindow.h"
#include <QMessageBox>
#include <qdebug.h>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    loginDialog_ = new LoginDialog(this);
    connect(loginDialog_, &LoginDialog::SigSwitchRegister, this, &MainWindow::OnSwitchRegisterDialog);
    connect(loginDialog_, &LoginDialog::SigSwitchReset, this, &MainWindow::OnSwitchResetDialog);

    // MainWindow 通过 AuthService 监听登录成功（不再直接依赖 HttpManager/TcpManager）
    connect(&AuthService::GetInstance(), &AuthService::SigLoginSuccess, this, &MainWindow::OnLoginSuccess);
    // 被顶号踢下线：弹窗提示并退回登录页
    connect(&AuthService::GetInstance(), &AuthService::SigKicked, this, &MainWindow::OnKicked);

    setCentralWidget(loginDialog_);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::OnSwitchRegisterDialog()
{
    if (loginDialog_) {
        loginDialog_->deleteLater();
        loginDialog_ = nullptr;
    }

    if (!registerDialog_) {
        registerDialog_ = new RegisterDialog(this);
        connect(registerDialog_, &RegisterDialog::SigSwitchLogin, this, &MainWindow::OnSwitchLoginDialog);
    }

    setCentralWidget(registerDialog_);
}

void MainWindow::OnSwitchLoginDialog()
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
        connect(loginDialog_, &LoginDialog::SigSwitchRegister, this, &MainWindow::OnSwitchRegisterDialog);
        connect(loginDialog_, &LoginDialog::SigSwitchReset, this, &MainWindow::OnSwitchResetDialog);
    }
    setCentralWidget(loginDialog_);
}

void MainWindow::OnSwitchResetDialog()
{
    if (loginDialog_) {
        loginDialog_->deleteLater();
        loginDialog_ = nullptr;
    }

    if (!resetDialog_) {
        resetDialog_ = new ResetDialog(this);
        connect(resetDialog_, &ResetDialog::SigSwitchLogin, this, &MainWindow::OnSwitchLoginDialog);
    }

    setCentralWidget(resetDialog_);
}

void MainWindow::OnLoginSuccess()
{
    if (loginDialog_) {
        loginDialog_->deleteLater();
        loginDialog_ = nullptr;
    }
    if (!mainPanel_) {
        mainPanel_ = new MainPanel(this);
    }
    this->setMinimumSize(QSize(1050, 900));
    this->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    setCentralWidget(mainPanel_);
}

void MainWindow::OnKicked(const QString &msg)
{
    // 被顶号踢下线：销毁聊天窗口，退回登录页
    if (mainPanel_) {
        mainPanel_->deleteLater();
        mainPanel_ = nullptr;
    }
    if (!loginDialog_) {
        loginDialog_ = new LoginDialog(this);
        connect(loginDialog_, &LoginDialog::SigSwitchRegister, this, &MainWindow::OnSwitchRegisterDialog);
        connect(loginDialog_, &LoginDialog::SigSwitchReset, this, &MainWindow::OnSwitchResetDialog);
    }
    // 恢复登录页固定尺寸（与 mainwindow.ui 一致：300x500）
    this->setMinimumSize(QSize(300, 500));
    this->setMaximumSize(QSize(300, 500));
    setCentralWidget(loginDialog_);
    QMessageBox::warning(this, tr("被迫下线"), msg);
}
