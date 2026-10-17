#include "test_framework.h"

#include <cstddef>
#include <cstdint>

#include "business_worker.h"

namespace {

using P1::SelectBusinessWorker;

TESTFW_TEST(SelectBusinessWorker_handles_degenerate_counts)
{
    CHECK_EQ(SelectBusinessWorker(1234, 0), 0u);
    CHECK_EQ(SelectBusinessWorker(1234, 1), 0u);
}

TESTFW_TEST(SelectBusinessWorker_is_stable_for_same_session)
{
    const std::size_t first = SelectBusinessWorker(1001, 8);
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(SelectBusinessWorker(1001, 8), first);
    }
}

TESTFW_TEST(SelectBusinessWorker_always_in_range)
{
    constexpr std::size_t kWorkerCount = 7;
    for (uint32_t sessionId = 1; sessionId <= 10000; ++sessionId) {
        CHECK(SelectBusinessWorker(sessionId, kWorkerCount) < kWorkerCount);
    }
}

} // namespace
