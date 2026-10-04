#include "mainwindow.h"
#include "mainpanel.h"
#include "logger.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
// 解耦网络处理层，UI层，JSON处理层，工具层
#define USE_FULL_LOGIN_FLOW 1
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // 安装日志处理器：所有 LOG_XXX 同时写入 <可执行文件目录>/logs/chatclient-<PID>.log 和 stderr。
    // GUI 程序在 Windows 下无控制台，仅靠 qDebug 默认输出会"丢"，必须落盘。
    // 文件名带 PID：多开客户端测试时每个实例写各自文件，日志不会交叉混写。
    const QString logDir = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    QDir().mkpath(logDir);
    Logger::Install(logDir + QStringLiteral("/chatclient-%1.log").arg(QCoreApplication::applicationPid()));
    LOG_INFO() << "ChatClient starting, log file:" << Logger::LogFilePath();
    QFile qss(":/style/stylesheet.qss");
    if (qss.open(QFile::ReadOnly)) {
        LOG_INFO() << "open stylesheet success";
        // QTextStream 按 UTF-8 解码并自动跳过 BOM，避免 BOM 导致 QSS 解析失败
        QTextStream ts(&qss);
        a.setStyleSheet(ts.readAll());
        qss.close();
    } else {
        LOG_WARN() << "Open stylesheet failed";
    }
#if USE_FULL_LOGIN_FLOW
    MainWindow w;
#else
    MainPanel w;
#endif
    w.show();
    return a.exec();
}
