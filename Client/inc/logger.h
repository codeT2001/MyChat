#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QDebug>

// 日志安装器：把 Qt 日志（qDebug/qInfo/qWarning/qCritical，即 LOG_XXX 宏）
// 同时写入日志文件和 stderr。
//
// 背景：客户端是 GUI 程序（Windows 下为 WIN32 子系统，无控制台），
// Qt 默认处理器只输出到控制台/调试器，直接运行或 VSCode 启动时
// 经常看不到任何输出。落盘后日志不再依赖启动方式。
//
// 用法（main.cpp，QApplication 构造之后）：
//   Logger::Install(logDir + "/chatclient-<PID>.log");
namespace Logger {

// 安装全局消息处理器。filePath 为日志文件完整路径，所在目录需已存在。
void Install(const QString &filePath);

// 当前日志文件路径（Install 之前调用返回空串）
QString LogFilePath();

} // namespace Logger

// ============================================
// 统一日志规范
// ============================================
// LOG_DEBUG: 调试信息，Release 模式下自动移除
// LOG_INFO:  关键状态信息（连接成功、登录成功等）
// LOG_WARN:  可恢复的错误/异常
// LOG_ERROR: 严重错误
//
// 使用示例:
//   LOG_INFO() << "Connected to" << host << port;
//   LOG_WARN() << "Invalid uid:" << uid;
// ============================================

#define LOG_DEBUG qDebug
#define LOG_INFO qInfo
#define LOG_WARN qWarning
#define LOG_ERROR qCritical

// Release 模式下禁用 DEBUG 日志
#ifdef QT_NO_DEBUG_OUTPUT
#undef LOG_DEBUG
#define LOG_DEBUG qNoDebug
#endif

#endif // LOGGER_H
