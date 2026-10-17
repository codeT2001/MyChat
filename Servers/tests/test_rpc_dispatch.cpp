#include "test_framework.h"

#include <cstddef>
#include <string>

#include "rpc_dispatcher.h"

TESTFW_TEST(SelectRpcWorker_handles_zero_and_one_worker)
{
    CHECK_EQ(P1::SelectRpcWorker("ChatServer2", 0), static_cast<std::size_t>(0));
    CHECK_EQ(P1::SelectRpcWorker("ChatServer2", 1), static_cast<std::size_t>(0));
}

TESTFW_TEST(SelectRpcWorker_is_stable_for_same_key)
{
    const std::size_t first = P1::SelectRpcWorker("ChatServer2", 7);
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(P1::SelectRpcWorker("ChatServer2", 7), first);
    }
}

TESTFW_TEST(SelectRpcWorker_always_in_range)
{
    const std::size_t workerCount = 5;
    for (int i = 0; i < 1000; ++i) {
        const std::string key = "ChatServer" + std::to_string(i);
        CHECK(P1::SelectRpcWorker(key, workerCount) < workerCount);
    }
}
