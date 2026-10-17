#include "test_framework.h"

#include <string>

#include "http_params.h"

namespace {

using P1::ParseRequestTarget;
using P1::UrlDecode;

// —— URL 解码 ——
TESTFW_TEST(UrlDecode_plain_text_is_unchanged)
{
    CHECK_EQ(UrlDecode("abcXYZ0189-_."), std::string("abcXYZ0189-_."));
    CHECK_EQ(UrlDecode(""), std::string(""));
}

TESTFW_TEST(UrlDecode_plus_becomes_space)
{
    CHECK_EQ(UrlDecode("a+b+c"), std::string("a b c"));
}

TESTFW_TEST(UrlDecode_percent_escapes)
{
    CHECK_EQ(UrlDecode("a%40b.com"), std::string("a@b.com"));       // 大写十六进制
    CHECK_EQ(UrlDecode("%2f%2F"), std::string("//"));               // 大小写等价
    CHECK_EQ(UrlDecode("%E4%BD%A0"), std::string("你"));            // UTF-8 多字节
    CHECK_EQ(UrlDecode("%20"), std::string(" "));
}

TESTFW_TEST(UrlDecode_malformed_escapes_are_kept_verbatim)
{
    CHECK_EQ(UrlDecode("abc%"), std::string("abc%"));               // 截断
    CHECK_EQ(UrlDecode("50%"), std::string("50%"));
    CHECK_EQ(UrlDecode("a%zz"), std::string("a%zz"));               // 非十六进制
    CHECK_EQ(UrlDecode("100%25"), std::string("100%"));             // 合法的转义仍然解码
}

// —— target 解析 ——
TESTFW_TEST(ParseRequestTarget_without_query)
{
    const auto parsed = ParseRequestTarget("/get_test");
    CHECK_EQ(parsed.path, std::string("/get_test"));
    CHECK(parsed.params.empty());
}

TESTFW_TEST(ParseRequestTarget_splits_path_and_params)
{
    const auto parsed = ParseRequestTarget("/get_test?key1=value1&key2=value2");
    CHECK_EQ(parsed.path, std::string("/get_test"));
    CHECK_EQ(parsed.params.size(), 2u);
    CHECK_EQ(parsed.params.at("key1"), std::string("value1"));
    CHECK_EQ(parsed.params.at("key2"), std::string("value2"));
}

TESTFW_TEST(ParseRequestTarget_decodes_keys_and_values)
{
    const auto parsed = ParseRequestTarget("/reset?email=a%40b.com&name=%E4%BD%A0+%E5%A5%BD");
    CHECK_EQ(parsed.params.at("email"), std::string("a@b.com"));
    CHECK_EQ(parsed.params.at("name"), std::string("你 好"));
}

TESTFW_TEST(ParseRequestTarget_handles_edges)
{
    // 空查询串
    auto parsed = ParseRequestTarget("/p?");
    CHECK_EQ(parsed.path, std::string("/p"));
    CHECK(parsed.params.empty());

    // 片段没有 '=' 时忽略，不影响其它参数
    parsed = ParseRequestTarget("/p?novalue&a=1");
    CHECK_EQ(parsed.params.size(), 1u);
    CHECK_EQ(parsed.params.at("a"), std::string("1"));

    // 值里含空串 / 结尾多余的 '&'
    parsed = ParseRequestTarget("/p?a=&b=2&");
    CHECK_EQ(parsed.params.at("a"), std::string(""));
    CHECK_EQ(parsed.params.at("b"), std::string("2"));
    CHECK_EQ(parsed.params.size(), 2u);

    // 值里出现 '='（只按第一个 '=' 切分）
    parsed = ParseRequestTarget("/p?token=a=b");
    CHECK_EQ(parsed.params.at("token"), std::string("a=b"));

    // 重复键：后值覆盖前值
    parsed = ParseRequestTarget("/p?a=1&a=2");
    CHECK_EQ(parsed.params.at("a"), std::string("2"));
    CHECK_EQ(parsed.params.size(), 1u);
}

} // namespace
