#ifndef _P1_LOG_H_
#define _P1_LOG_H_

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <mutex>

namespace P1 {
namespace detail {

inline std::mutex& GetLogMutex()
{
    // 故意不析构，规避静态对象析构顺序不确定导致的日志崩溃
    static std::mutex* mtx = new std::mutex();
    return *mtx;
}

inline void LogPrint(const char* level, const char* fmt, ...)
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_r(&t, &tm);

    std::lock_guard<std::mutex> lock(GetLogMutex());
    std::fprintf(stderr, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%-5s] ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()), level);

    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);

    std::fprintf(stderr, "\n");
}

} // namespace detail
} // namespace P1

#define LOG_DEBUG(...) P1::detail::LogPrint("DEBUG", __VA_ARGS__)
#define LOG_INFO(...)  P1::detail::LogPrint("INFO",  __VA_ARGS__)
#define LOG_WARN(...)  P1::detail::LogPrint("WARN",  __VA_ARGS__)
#define LOG_ERROR(...) P1::detail::LogPrint("ERROR", __VA_ARGS__)

#endif // _P1_LOG_H_
