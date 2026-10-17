#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <QFile>
#include <QRegularExpression>
#include <QString>

// 测试专用的小工具：读取源码树文件 + 从 C++ 枚举源码里取数值。
// 路径由 CMake 通过编译期宏注入（见 Client/tests/CMakeLists.txt）。
namespace testhelp {

inline QString readFile(const QString &absolutePath)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// 相对 Client/ 根（源码/资源）
inline QString readClientFile(const QString &relative)
{
    return readFile(QString::fromUtf8(CHAT_CLIENT_ROOT) + "/" + relative);
}

// 相对仓库根（跨项目契约：Servers/... 的权威定义）
inline QString readRepoFile(const QString &relative)
{
    return readFile(QString::fromUtf8(CHAT_REPO_ROOT) + "/" + relative);
}

// 从源码里取 NAME = <整数表达式> 的值（支持 `1024 * 2` 这类乘积写法，枚举项以 , 或 ; 结尾）；
// 找不到返回 INT_MIN
inline int symbolValue(const QString &source, const QString &name)
{
    const QRegularExpression re(QStringLiteral("\\b%1\\s*=\\s*([0-9][0-9\\s\\*]*)(?=[,;])").arg(name));
    const QRegularExpressionMatch match = re.match(source);
    if (!match.hasMatch()) {
        return INT_MIN;
    }
    int value = 1;
    const QStringList factors = match.captured(1).split(QLatin1Char('*'), Qt::SkipEmptyParts);
    for (const QString &factor : factors) {
        value *= factor.trimmed().toInt();
    }
    return value;
}

} // namespace testhelp

#endif // TEST_HELPERS_H
