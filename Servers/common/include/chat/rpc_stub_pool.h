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

// 轻量 gRPC stub 池：固定数量 stub + 带超时的 Acquire + RAII Lease。
// channel 使用 gRPC 默认参数；valid=false 表示初始化失败，不按池耗尽超时统计。
template<typename ServiceType>
class RpcStubPool {
public:
    using StubType = typename ServiceType::Stub;

    static constexpr size_t kDefaultPoolSize = 5;
    static constexpr int64_t kDefaultAcquireTimeoutMs = 1000;
    static constexpr int64_t kDestroyWaitMs = 5000;

    struct Options {
        size_t poolSize = kDefaultPoolSize;
        std::chrono::milliseconds acquireTimeout {kDefaultAcquireTimeoutMs};
    };

    struct Stats {
        size_t available = 0;
        size_t inUse = 0;
        size_t waiting = 0;
        uint64_t timeouts = 0;
        bool valid = false;
    };

private:
    struct State {
        std::mutex mutex;
        std::condition_variable cond;
        std::queue<std::unique_ptr<StubType>> stubs;
        std::chrono::milliseconds acquireTimeout {kDefaultAcquireTimeoutMs};
        std::string target;
        size_t inUse = 0;
        size_t waiting = 0;
        uint64_t timeouts = 0;
        bool stop = false;
        bool valid = false;
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
            }
            if (state->inUse > 0) {
                --state->inUse;
            }
            // 唤醒等待租约的 Acquire，以及析构时等待 inUse==0 的线程
            state->cond.notify_all();
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
        : RpcStubPool(Options {poolSize, std::chrono::milliseconds(kDefaultAcquireTimeoutMs)}, host, port)
    {
    }

    RpcStubPool(const Options& options, const std::string& host, const std::string& port)
        : state_(std::make_shared<State>())
    {
        state_->acquireTimeout = std::max(options.acquireTimeout, std::chrono::milliseconds(0));
        state_->target = host + ":" + port;

        auto channel = grpc::CreateChannel(state_->target, grpc::InsecureChannelCredentials());
        if (channel) {
            const size_t poolSize = std::max<size_t>(options.poolSize, 1);
            for (size_t i = 0; i < poolSize; ++i) {
                if (auto stub = ServiceType::NewStub(channel)) {
                    state_->stubs.push(std::move(stub));
                }
            }
        }

        state_->valid = !state_->stubs.empty();
        if (!state_->valid) {
            LOG_ERROR("[RpcStubPool] init failed, target:%s", state_->target.c_str());
            return;
        }
        LOG_INFO("[RpcStubPool] initialized, target:%s, stub count:%zu",
            state_->target.c_str(), state_->stubs.size());
    }

    ~RpcStubPool()
    {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->stop = true;
        state_->cond.notify_all();

        if (state_->inUse > 0 &&
            !state_->cond.wait_for(lock, std::chrono::milliseconds(kDestroyWaitMs),
                [this]() { return state_->inUse == 0; })) {
            LOG_WARN("[RpcStubPool] destroy wait timeout, target:%s, inUse:%zu",
                state_->target.c_str(), state_->inUse);
        }

        while (!state_->stubs.empty()) {
            state_->stubs.pop();
        }
    }

    RpcStubPool(const RpcStubPool&) = delete;
    RpcStubPool& operator=(const RpcStubPool&) = delete;

    Lease Acquire() { return Acquire(state_->acquireTimeout); }

    Lease Acquire(std::chrono::milliseconds timeout)
    {
        auto state = state_;
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->valid) {
            return Lease(); // 初始化失败：立即返回，不计入超时
        }

        ++state->waiting;
        const bool ready = state->cond.wait_for(lock, std::max(timeout, std::chrono::milliseconds(0)),
            [&state]() { return state->stop || !state->stubs.empty(); });
        --state->waiting;

        if (!ready) {
            ++state->timeouts;
            LOG_WARN("[RpcStubPool] acquire timeout, target:%s, timeout:%lldms, inUse:%zu, waiting:%zu",
                state->target.c_str(),
                static_cast<long long>(std::max(timeout, std::chrono::milliseconds(0)).count()),
                state->inUse, state->waiting);
            return Lease();
        }
        if (state->stop || state->stubs.empty()) {
            return Lease();
        }

        auto stub = std::move(state->stubs.front());
        state->stubs.pop();
        ++state->inUse;
        return Lease(std::move(state), std::move(stub));
    }

    Stats GetStats() const
    {
        auto state = state_;
        std::lock_guard<std::mutex> lock(state->mutex);
        Stats stats;
        stats.available = state->stubs.size();
        stats.inUse = state->inUse;
        stats.waiting = state->waiting;
        stats.timeouts = state->timeouts;
        stats.valid = state->valid;
        return stats;
    }

private:
    std::shared_ptr<State> state_;
};
} // namespace P1
#endif
