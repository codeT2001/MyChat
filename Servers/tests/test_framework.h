#ifndef _CHAT_TEST_FRAMEWORK_H_
#define _CHAT_TEST_FRAMEWORK_H_

// 极简单文件测试框架（零外部依赖，兼容 C++17）。
//
// 用法：
//   TESTFW_TEST(MyCase) {
//       CHECK(cond);
//       CHECK_EQ(actual, expected);
//   }
// 由 tests/test_main.cpp 里的 main() 调用 testfw::RunAll() 统一执行；
// 每个测试可执行文件都要链接 test_main.cpp（提供 main）。
//
// 约定：一个测试可执行文件 = test_main.cpp + 若干用例文件（可多文件共享注册表）。

#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace testfw {

struct TestCase {
    std::string name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

inline int& FailureCount()
{
    static int count = 0;
    return count;
}

inline void ReportFailure(const char* file, int line, const char* detail)
{
    ++FailureCount();
    // 统一走 stdout：避免 stdout/stderr 缓冲差异在被重定向时打乱输出顺序
    std::printf("    FAIL %s:%d: %s\n", file, line, detail);
}

inline void Check(bool cond, const char* file, int line, const char* expr)
{
    if (!cond) {
        ReportFailure(file, line, expr);
    }
}

// 是否可流式输出（用于在失败时打印实际值/期望值）
template <typename T, typename = void>
struct IsStreamable : std::false_type {};

template <typename T>
struct IsStreamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <typename T>
std::string DebugString(const T& value)
{
    if constexpr (IsStreamable<T>::value) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return std::string("<unprintable>");
    }
}

template <typename A, typename B>
void CheckEq(const A& actual, const B& expected, const char* file, int line, const char* expr)
{
    if (!(actual == expected)) {
        const std::string detail = std::string(expr) +
            "  (actual: " + DebugString(actual) + ", expected: " + DebugString(expected) + ")";
        ReportFailure(file, line, detail.c_str());
    }
}

inline int RunAll()
{
    // 关掉 stdout 缓冲，避免与无缓冲的 stderr 交错导致失败信息错位
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int failedCases = 0;
    for (const auto& tc : Registry()) {
        const int before = FailureCount();
        std::printf("[ RUN  ] %s\n", tc.name.c_str());
        std::fflush(stdout);
        try {
            tc.fn();
        } catch (const std::exception& e) {
            ReportFailure("<exception>", 0, e.what());
        } catch (...) {
            ReportFailure("<exception>", 0, "unknown exception");
        }
        if (FailureCount() == before) {
            std::printf("[  OK  ] %s\n", tc.name.c_str());
        } else {
            std::printf("[ FAIL ] %s\n", tc.name.c_str());
            ++failedCases;
        }
    }
    std::printf("\n%d case(s), %d failed case(s), %d failed check(s)\n",
        static_cast<int>(Registry().size()), failedCases, FailureCount());
    return failedCases == 0 ? 0 : 1;
}

} // namespace testfw

#define TESTFW_TEST(name) \
    static void name(); \
    static const testfw::Registrar testfw_reg_##name(#name, &name); \
    static void name()

#define CHECK(cond) testfw::Check((cond), __FILE__, __LINE__, #cond)
#define CHECK_EQ(actual, expected) \
    testfw::CheckEq((actual), (expected), __FILE__, __LINE__, #actual " == " #expected)

#endif // _CHAT_TEST_FRAMEWORK_H_
