#ifndef LOG_H
#define LOG_H

#include <QDebug>

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

#endif // LOG_H
