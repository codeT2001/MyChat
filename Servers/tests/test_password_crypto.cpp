#include "test_framework.h"

#include <string>

#include "chat/password_crypto.h"

using P1::ConstantTimeEquals;
using P1::HashPassword;

TESTFW_TEST(HashPassword_IsDeterministic)
{
    CHECK_EQ(HashPassword("123456"), HashPassword("123456"));
    CHECK_EQ(HashPassword(""), HashPassword(""));
}

TESTFW_TEST(HashPassword_FixedLengthLowerHex)
{
    CHECK_EQ(HashPassword("").size(), static_cast<size_t>(64));
    CHECK_EQ(HashPassword("a").size(), static_cast<size_t>(64));
    CHECK_EQ(HashPassword(std::string(1000, 'x')).size(), static_cast<size_t>(64));
    for (char c : HashPassword("Secret#1")) {
        CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TESTFW_TEST(HashPassword_DiffersForDifferentInputs)
{
    CHECK(HashPassword("123456") != HashPassword("123457"));
    CHECK(HashPassword("abc") != HashPassword("ABC"));
    CHECK(HashPassword("") != HashPassword(" "));
}

TESTFW_TEST(HashPassword_Avalanche)
{
    const std::string a = HashPassword("123456");
    const std::string b = HashPassword("123457");
    int diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            ++diff;
        }
    }
    // 合格的单向散列：相邻输入应有明显（> 50%）的十六进制位差异
    CHECK(diff > 32);
}

TESTFW_TEST(ConstantTimeEquals_Behaviour)
{
    const std::string h = HashPassword("secret");
    CHECK(ConstantTimeEquals(h, h));
    CHECK(!ConstantTimeEquals(h, HashPassword("secret2")));
    CHECK(!ConstantTimeEquals("abc", "abcd")); // 长度不同
    CHECK(ConstantTimeEquals("", ""));
}

TESTFW_TEST(PasswordFlow_StoreAndVerify)
{
    // 模拟注册（存摘要）→ 登录（比对摘要）的完整链路
    const std::string stored = HashPassword("P@ssw0rd!");
    CHECK(ConstantTimeEquals(stored, HashPassword("P@ssw0rd!")));
    CHECK(!ConstantTimeEquals(stored, HashPassword("p@ssw0rd!")));
}
