#include "mainwindow.h"
#include "applyfrienddialog.h"
#include "chatwindow.h"
#include "log.h"
#include <QApplication>
#include <QFile>
#include <QTextStream>
// 解耦网络处理层，UI层，JSON处理层，工具层
#define NO_DEBUG 1
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
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
#if NO_DEBUG
    MainWindow w;
#else
    ChatWindow w;
#endif
    w.show();
    return a.exec();
}
