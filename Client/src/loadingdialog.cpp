#include "loadingdialog.h"
#include "ui_loadingdialog.h"
#include <QLabel>
#include <QMovie>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QScreen>
LoadingDialog::LoadingDialog(QWidget *parent) : QDialog(parent), ui(new Ui::LoadingDialog)
{
    ui->setupUi(this);
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground); // 设置背景透明
    // 获取屏幕尺寸
    setFixedSize(parent->size()); // 设置对话框为全屏尺寸

    movie_ = new QMovie(":/images/loading.gif"); // 加载动画的资源文件
    ui->loadingLb->setMovie(movie_);
}
void LoadingDialog::Start()
{
    movie_->start();
}

void LoadingDialog::Stop()
{
    movie_->stop();
}
LoadingDialog::~LoadingDialog()
{
    delete ui;
}
