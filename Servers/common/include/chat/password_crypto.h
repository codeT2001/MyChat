#ifndef _CHAT_PASSWORD_CRYPTO_H_
#define _CHAT_PASSWORD_CRYPTO_H_

#include <string>

namespace P1 {

// 口令摘要：SHA-256(固定盐 + 口令)，输出 64 位小写十六进制。
// 输出定长 64，正好适配 user.pwd VARCHAR(64)（无需改表）；为单向哈希，不可反解。
// 说明：这里用编译期固定盐（pepper）而非逐用户随机盐，省去额外存储列；
// 属"简单实现"，生产环境应改用 bcrypt / argon2id 等带随机盐的慢哈希。
// 同一输入结果恒定，因此仍可用于等值比较。
std::string HashPassword(const std::string& pwd);

// 恒定时间字符串比较：逐字节比较不短路，避免通过响应时序侧信道推断内容。
// 用于口令摘要比对。长度不同直接返回 false（摘要定长 64，长度本身非敏感信息）。
bool ConstantTimeEquals(const std::string& a, const std::string& b);

} // namespace P1

#endif // _CHAT_PASSWORD_CRYPTO_H_
