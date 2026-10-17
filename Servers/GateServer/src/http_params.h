#ifndef _P1_HTTP_PARAMS_H_
#define _P1_HTTP_PARAMS_H_

#include <map>
#include <string>
#include <string_view>

namespace P1 {

// URL 解码：'+' → 空格；"%XX" → 对应字节（大小写十六进制均可）。
// 非法或截断的 % 序列（如 "abc%"、"a%zz"）原样保留，不抛异常也不丢字符。
std::string UrlDecode(const std::string& src);

// 解析后的请求 target
struct ParsedTarget {
    std::string path;                          // 问号前的路径；无问号时等于整个 target
    std::map<std::string, std::string> params; // 查询串键值对（键与值均已解码）
};

// 解析 "/get_test?key1=value1&key2=value2"：
// 无 '=' 的片段被忽略；重复键后值覆盖前值
ParsedTarget ParseRequestTarget(std::string_view target);

} // namespace P1

#endif // _P1_HTTP_PARAMS_H_
