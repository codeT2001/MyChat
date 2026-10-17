#include "test_framework.h"

#include <memory>
#include <string>

#include "chat/session_registry.h"

namespace {

using IntPtr = std::shared_ptr<int>;
using Registry = P1::SessionRegistry<IntPtr>;

IntPtr MakeSession(int marker)
{
    return std::make_shared<int>(marker);
}

TESTFW_TEST(SessionRegistry_get_missing_returns_null)
{
    Registry reg;
    CHECK(reg.Get(1001) == nullptr);
    CHECK_EQ(reg.Size(), 0u);
}

TESTFW_TEST(SessionRegistry_set_overwrites_previous_binding)
{
    Registry reg;
    auto oldSession = MakeSession(1);
    auto newSession = MakeSession(2);

    reg.Set(1001, oldSession);
    CHECK(reg.Get(1001) == oldSession);

    reg.Set(1001, newSession);          // 顶号：新会话覆盖旧绑定
    CHECK(reg.Get(1001) == newSession);
    CHECK_EQ(reg.Size(), 1u);
}

TESTFW_TEST(SessionRegistry_remove_matching_session)
{
    Registry reg;
    auto session = MakeSession(1);
    reg.Set(1001, session);

    CHECK(reg.RemoveIfMatch(1001, session));
    CHECK(reg.Get(1001) == nullptr);
    CHECK_EQ(reg.Size(), 0u);
}

TESTFW_TEST(SessionRegistry_remove_stale_session_is_rejected)
{
    // 被顶掉的旧会话断连时不得删掉当前生效的新绑定
    Registry reg;
    auto oldSession = MakeSession(1);
    auto newSession = MakeSession(2);
    reg.Set(1001, newSession);

    CHECK(!reg.RemoveIfMatch(1001, oldSession));
    CHECK(reg.Get(1001) == newSession);   // 新绑定仍在
    CHECK_EQ(reg.Size(), 1u);
}

TESTFW_TEST(SessionRegistry_remove_unknown_uid_is_rejected)
{
    Registry reg;
    CHECK(!reg.RemoveIfMatch(9999, MakeSession(1)));
    CHECK_EQ(reg.Size(), 0u);
}

TESTFW_TEST(SessionRegistry_keeps_uids_isolated)
{
    Registry reg;
    auto s1 = MakeSession(1);
    auto s2 = MakeSession(2);
    reg.Set(1001, s1);
    reg.Set(1002, s2);

    CHECK(reg.Get(1001) == s1);
    CHECK(reg.Get(1002) == s2);
    CHECK(reg.RemoveIfMatch(1001, s1));
    CHECK(reg.Get(1002) == s2);
    CHECK_EQ(reg.Size(), 1u);
}

TESTFW_TEST(SessionRegistry_null_session_binding_is_removable)
{
    // Set(uid, nullptr) 后原样传 nullptr 视为匹配（保持旧实现的比较语义）
    Registry reg;
    reg.Set(1001, nullptr);
    CHECK(reg.Get(1001) == nullptr);
    CHECK(reg.RemoveIfMatch(1001, nullptr));
    CHECK_EQ(reg.Size(), 0u);
}

} // namespace
