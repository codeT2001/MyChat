#ifndef _P1_LOG_H_
#define _P1_LOG_H_

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <mutex>

namespace P1 {
namespace detail {

// 编译期取文件名（去掉目录部分），兼容 Linux 的 '/' 与 Windows 的 '\'
constexpr const char* BaseName(const char* path)
{
    const char* base = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    return base;
}

inline std::mutex& GetLogMutex()
{
    // 故意不析构，规避静态对象析构顺序不确定导致的日志崩溃
    static std::mutex* mtx = new std::mutex();
    return *mtx;
}

inline void LogPrint(const char* file, const char* func, int line,
                     const char* level, const char* fmt, ...)
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_r(&t, &tm);

    std::lock_guard<std::mutex> lock(GetLogMutex());
    std::fprintf(stderr, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%-5s] [%s:%d %s] ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()),
        level, BaseName(file), line, func);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);

    std::fprintf(stderr, "\n");
}

} // namespace detail
} // namespace P1

#define LOG_DEBUG(...) P1::detail::LogPrint(__FILE__, __func__, __LINE__, "DEBUG", __VA_ARGS__)
#define LOG_INFO(...)  P1::detail::LogPrint(__FILE__, __func__, __LINE__, "INFO",  __VA_ARGS__)
#define LOG_WARN(...)  P1::detail::LogPrint(__FILE__, __func__, __LINE__, "WARN",  __VA_ARGS__)
#define LOG_ERROR(...) P1::detail::LogPrint(__FILE__, __func__, __LINE__, "ERROR", __VA_ARGS__)

#endif // _P1_LOG_H_
