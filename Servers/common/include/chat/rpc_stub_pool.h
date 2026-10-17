#ifndef _RPC_STUB_POOL_H_
#define _RPC_STUB_POOL_H_

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <utility>

#include "chat/log.h"

namespace P1 {

// gRPC stub 连接池（header-only）。
//
// 设计要点：
//  1. 所有 stub 共享同一个 channel，channel 自身负责重连；池的作用是限制同时在途
//     的 RPC 数，避免对端变慢时把调用方线程和内存拖垮。
//  2. Acquire(timeout) 在池耗尽时按超时快速失败，不再无限阻塞调用线程。
//  3. Lease 是 RAII 句柄，析构自动归还；异常或提前 return 不会漏掉池内 stub。
//  4. GetStats() 暴露池状态，便于定位池耗尽和超时。
//
// 用法：
//     auto lease = pool->Acquire();
//     if (!lease) { /* 池不可用或获取超时 */ }
//     lease->SomeRpc(&context, request, &response);  // 离开作用域自动归还
template<typename ServiceType>
class RpcStubPool {
public:
    using StubType = typename ServiceType::Stub;

    static constexpr size_t kDefaultPoolSize = 5;
    static constexpr int64_t kDefaultAcquireTimeoutMs = 1000;

    struct Options {
        size_t poolSize = kDefaultPoolSize;
        std::chrono::milliseconds acquireTimeout {kDefaultAcquireTimeoutMs};
        bool keepalive = true;
    };

    struct Stats {
        size_t poolSize = 0;
        size_t created = 0;
        size_t available = 0;
        size_t inUse = 0;
        size_t waiting = 0;
        uint64_t acquisitions = 0;
        uint64_t timeouts = 0;
    };

private:
    struct State {
        std::mutex mutex;
        std::condition_variable cond;
        std::queue<std::unique_ptr<StubType>> stubs;
        bool stop = false;
        size_t poolSize = 0;
        size_t created = 0;
        size_t waiting = 0;
        uint64_t acquisitions = 0;
        uint64_t timeouts = 0;
        std::chrono::milliseconds acquireTimeout {kDefaultAcquireTimeoutMs};
        std::string target;
    };

public:
    class Lease {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept
            : state_(std::move(other.state_)), stub_(std::move(other.stub_))
        {
        }

        Lease& operator=(Lease&& other) noexcept
        {
            if (this != &other) {
                Reset();
                state_ = std::move(other.state_);
                stub_ = std::move(other.stub_);
            }
            return *this;
        }

        ~Lease() { Reset(); }

        explicit operator bool() const noexcept { return stub_ != nullptr; }
        StubType* get() const noexcept { return stub_.get(); }
        StubType* operator->() const noexcept { return stub_.get(); }
        StubType& operator*() const noexcept { return *stub_; }

        // 提前归还。Lease 持有 State 的 shared_ptr，因此即使池对象先析构也安全。
        void Reset() noexcept
        {
            if (!stub_) {
                state_.reset();
                return;
            }
            auto state = std::move(state_);
            auto stub = std::move(stub_);
            if (!state) {
                return;
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->stop) {
                state->stubs.push(std::move(stub));
                state->cond.notify_one();
            }
        }

    private:
        friend class RpcStubPool;

        Lease(std::shared_ptr<State> state, std::unique_ptr<StubType> stub)
            : state_(std::move(state)), stub_(std::move(stub))
        {
        }

        std::shared_ptr<State> state_;
        std::unique_ptr<StubType> stub_;
    };

    RpcStubPool(size_t poolSize, const std::string& host, const std::string& port)
        : RpcStubPool(Options {poolSize, std::chrono::milliseconds(kDefaultAcquireTimeoutMs), true}, host, port)
    {
    }

    RpcStubPool(const Options& options, const std::string& host, const std::string& port)
        : state_(std::make_shared<State>())
    {
        state_->poolSize = std::max<size_t>(options.poolSize, 1);
        state_->acquireTimeout = std::max(options.acquireTimeout, std::chrono::milliseconds(0));
        state_->target = host + ":" + port;

        grpc::ChannelArguments channelArgs;
        if (options.keepalive) {
            // 尽早发现半开连接，并允许无在途 RPC 时发送 ping，避免 NAT/代理静默回收。
            channelArgs.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 30000);
            channelArgs.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);
            channelArgs.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
            channelArgs.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
        }
        // 对端重启后尽快重连；gRPC 默认最大退避 120s，故障恢复太慢。
        channelArgs.SetInt(GRPC_ARG_INITIAL_RECONNECT_BACKOFF_MS, 500);
        channelArgs.SetInt(GRPC_ARG_MIN_RECONNECT_BACKOFF_MS, 1000);
        channelArgs.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 5000);

        auto channel = grpc::CreateCustomChannel(
            state_->target, grpc::InsecureChannelCredentials(), channelArgs);
        if (!channel) {
            LOG_ERROR("[RpcStubPool] create channel failed, target:%s", state_->target.c_str());
            return;
        }

        for (size_t i = 0; i < state_->poolSize; ++i) {
            auto stub = ServiceType::NewStub(channel);
            if (!stub) {
                continue;
            }
            state_->stubs.push(std::move(stub));
            ++state_->created;
        }
        if (state_->created == 0) {
            LOG_ERROR("[RpcStubPool] no stub created, target:%s", state_->target.c_str());
            return;
        }
        LOG_INFO("[RpcStubPool] initialized, target:%s, stub count:%zu, acquire timeout:%lldms",
            state_->target.c_str(), state_->created,
            static_cast<long long>(state_->acquireTimeout.count()));
    }

    ~RpcStubPool()
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->stop = true;
            while (!state_->stubs.empty()) {
                state_->stubs.pop();
            }
        }
        state_->cond.notify_all();
    }

    RpcStubPool(const RpcStubPool&) = delete;
    RpcStubPool& operator=(const RpcStubPool&) = delete;

    Lease Acquire() { return Acquire(state_->acquireTimeout); }

    Lease Acquire(std::chrono::milliseconds timeout)
    {
        auto state = state_;
        const auto waitTime = std::max(timeout, std::chrono::milliseconds(0));
        std::unique_lock<std::mutex> lock(state->mutex);

        ++state->waiting;
        const bool ready = state->cond.wait_for(lock, waitTime, [&state]() {
            return state->stop || state->created == 0 || !state->stubs.empty();
        });
        --state->waiting;

        if (!ready || state->stop || state->stubs.empty()) {
            // created == 0 表示初始化失败，不按“池耗尽超时”计数。
            if (!state->stop && state->created > 0) {
                ++state->timeouts;
                LOG_WARN("[RpcStubPool] acquire timeout, target:%s, timeout:%lldms, created:%zu, inUse:%zu, waiting:%zu",
                    state->target.c_str(),
                    static_cast<long long>(waitTime.count()),
                    state->created,
                    state->created - state->stubs.size(),
                    state->waiting);
            }
            return Lease();
        }

        auto stub = std::move(state->stubs.front());
        state->stubs.pop();
        ++state->acquisitions;
        return Lease(std::move(state), std::move(stub));
    }

    Stats GetStats() const
    {
        auto state = state_;
        std::lock_guard<std::mutex> lock(state->mutex);
        Stats stats;
        stats.poolSize = state->poolSize;
        stats.created = state->created;
        stats.available = state->stubs.size();
        stats.inUse = state->created - state->stubs.size();
        stats.waiting = state->waiting;
        stats.acquisitions = state->acquisitions;
        stats.timeouts = state->timeouts;
        return stats;
    }

private:
    std::shared_ptr<State> state_;
};
} // namespace P1
#endif
