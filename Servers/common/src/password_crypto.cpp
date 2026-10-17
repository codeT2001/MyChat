#include "chat/password_crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <cstddef>

namespace P1 {
namespace {

// 固定盐（pepper）：与口令拼接后再哈希，避免仅拿到库就能用彩虹表直接撞库。
// 注意：这是编译进二进制的常量，仅作"简单实现"；正式环境应改为逐用户随机盐 + 慢哈希。
constexpr char kPasswordSalt[] = "MyChat::pwd::v1::7f3a9c";

} // namespace

std::string HashPassword(const std::string& pwd)
{
    std::string salted;
    salted.reserve(sizeof(kPasswordSalt) - 1 + pwd.size());
    salted.append(kPasswordSalt, sizeof(kPasswordSalt) - 1);
    salted.append(pwd);

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        return std::string();
    }
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, salted.data(), salted.size());
    EVP_DigestFinal_ex(ctx, digest, &digestLen);
    EVP_MD_CTX_free(ctx);

    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<size_t>(digestLen) * 2);
    for (unsigned int i = 0; i < digestLen; ++i) {
        out.push_back(kHex[digest[i] >> 4]);
        out.push_back(kHex[digest[i] & 0x0F]);
    }
    return out;
}

bool ConstantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace P1
