#include "test_framework.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "chat/rpc_stub_pool.h"

namespace {

struct FakeService {
    class Stub {
    public:
        explicit Stub(int id) : id(id) { }
        int id = 0;
    };

    static std::unique_ptr<Stub> NewStub(const std::shared_ptr<grpc::ChannelInterface>&)
    {
        static std::atomic<int> next {0};
        return std::make_unique<Stub>(next.fetch_add(1));
    }
};

using FakePool = P1::RpcStubPool<FakeService>;

FakePool::Options MakeOptions(size_t poolSize, int64_t timeoutMs)
{
    FakePool::Options options;
    options.poolSize = poolSize;
    options.acquireTimeout = std::chrono::milliseconds(timeoutMs);
    options.keepalive = false;
    return options;
}

TESTFW_TEST(RpcStubPool_AcquireUntilExhausted)
{
    FakePool pool(MakeOptions(2, 10), "127.0.0.1", "1");

    auto first = pool.Acquire();
    auto second = pool.Acquire();
    CHECK(static_cast<bool>(first));
    CHECK(static_cast<bool>(second));

    auto stats = pool.GetStats();
    CHECK_EQ(stats.created, static_cast<size_t>(2));
    CHECK_EQ(stats.available, static_cast<size_t>(0));
    CHECK_EQ(stats.inUse, static_cast<size_t>(2));

    auto third = pool.Acquire();
    CHECK(!static_cast<bool>(third));

    stats = pool.GetStats();
    CHECK_EQ(stats.created, static_cast<size_t>(2));
    CHECK_EQ(stats.timeouts, static_cast<uint64_t>(1));
}

TESTFW_TEST(RpcStubPool_LeaseReturnsStubAutomatically)
{
    FakePool pool(MakeOptions(1, 10), "127.0.0.1", "1");
    {
        auto lease = pool.Acquire();
        CHECK(static_cast<bool>(lease));
        CHECK_EQ(pool.GetStats().available, static_cast<size_t>(0));
    }

    CHECK_EQ(pool.GetStats().available, static_cast<size_t>(1));
    auto again = pool.Acquire();
    CHECK(static_cast<bool>(again));
}

TESTFW_TEST(RpcStubPool_WaitingAcquireGetsReturnedStub)
{
    FakePool pool(MakeOptions(1, 1000), "127.0.0.1", "1");
    auto lease = pool.Acquire();
    CHECK(static_cast<bool>(lease));

    std::atomic<bool> acquired {false};
    std::thread waiter([&]() {
        auto second = pool.Acquire();
        acquired.store(static_cast<bool>(second));
    });

    // 等待 waiter 进入条件变量，避免超时后误判
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (pool.GetStats().waiting == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_EQ(pool.GetStats().waiting, static_cast<size_t>(1));

    lease.Reset();
    waiter.join();
    CHECK(acquired.load());
}

TESTFW_TEST(RpcStubPool_LeaseOutlivesPool)
{
    auto pool = std::make_unique<FakePool>(MakeOptions(1, 10), "127.0.0.1", "1");
    auto lease = pool->Acquire();
    CHECK(static_cast<bool>(lease));

    pool.reset();
    lease.Reset();
}

} // namespace
