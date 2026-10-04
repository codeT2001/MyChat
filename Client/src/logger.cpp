#include "logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

namespace {

QMutex g_mutex;
QString g_logPath;

const char *LevelName(QtMsgType type)
{
    switch (type) {
        case QtDebugMsg:
            return "DEBUG";
        case QtInfoMsg:
            return "INFO ";
        case QtWarningMsg:
            return "WARN ";
        case QtCriticalMsg:
            return "ERROR";
        case QtFatalMsg:
            return "FATAL";
    }
    return "?????";
}

void Handler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    QString line = QStringLiteral("%1 [%2] %3").arg(timestamp, QLatin1String(LevelName(type)), msg);
    // 所有级别附带源码位置，精简格式：[文件名:行号, 函数名]
    // - 文件只留 basename，去掉冗长的绝对路径
    // - 函数去掉返回值与参数表："void AuthService::Login(const QString&, ...)" -> "AuthService::Login"
    if (context.file != nullptr) {
        const QString file = QString::fromLatin1(context.file);
        const int slash = qMax(file.lastIndexOf(QLatin1Char('/')), file.lastIndexOf(QLatin1Char('\\')));
        const QString baseName = slash >= 0 ? file.mid(slash + 1) : file;

        QString func = QString::fromLatin1(context.function ? context.function : "");
        const int paren = func.indexOf(QLatin1Char('('));
        if (paren >= 0) {
            func.truncate(paren);                       // 去掉参数表及之后
            const int space = func.lastIndexOf(QLatin1Char(' '));
            if (space >= 0) {
                func = func.mid(space + 1);             // 去掉返回值（如 "void "）
            }
        }
        line += QStringLiteral("  [%1:%2, %3]").arg(baseName).arg(context.line).arg(func);
    }
    line += QLatin1Char('\n');

    const QByteArray utf8 = line.toUtf8();

    // 写文件（追加模式，打开失败也继续走 stderr，不丢日志）
    {
        QMutexLocker locker(&g_mutex);
        QFile file(g_logPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            file.write(utf8);
        }
    }

    // 同步回显到 stderr：有控制台时（Linux/macOS、VSCode 集成终端）可直接看到
    std::fwrite(utf8.constData(), 1, utf8.size(), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

namespace Logger {

QString LogFilePath()
{
    QMutexLocker locker(&g_mutex);
    return g_logPath;
}

void Install(const QString &filePath)
{
    {
        QMutexLocker locker(&g_mutex);
        g_logPath = filePath;
    }
    qInstallMessageHandler(&Handler);
}

} // namespace Logger