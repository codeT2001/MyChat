#ifndef _RPC_STUB_POOL_H_
#define _RPC_STUB_POOL_H_

#include <atomic>
#include <condition_variable>
#include <grpcpp/grpcpp.h>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include "chat/log.h"

namespace P1 {

template<typename ServiceType>
class RpcStubPool {
public:
    using StubType = typename ServiceType::Stub;

    RpcStubPool(size_t poolSize, const std::string& host, const std::string& port)
        : poolSize_(poolSize), host_(host), port_(port), stop_(false), initFailed_(false)
    {
        std::string target = host_ + ":" + port_;
        auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());

        if (!channel) {
            LOG_ERROR("[RpcStubPool] create channel failed, target:%s", target.c_str());
            initFailed_ = true;
            return;
        }

        for (size_t i = 0; i < poolSize_; ++i) {
            auto stub = ServiceType::NewStub(channel);
            if (stub) {
                stubs_.push(std::move(stub));
            }
        }
        if (stubs_.empty()) {
            LOG_ERROR("[RpcStubPool] no stub created, target:%s", target.c_str());
            initFailed_ = true;
            return;
        }
        LOG_INFO("[RpcStubPool] initialized, target:%s, stub count:%zu", target.c_str(), stubs_.size());
    }

    ~RpcStubPool()
    {
        Close();
        std::lock_guard<std::mutex> lock(mutex_);
        while (!stubs_.empty()) {
            stubs_.pop();
        }
    }

    std::unique_ptr<StubType> GetStub()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // initFailed_ 时 stubs_ 永远为空，不能无限等待，否则调用线程会挂死
        cond_.wait(lock, [this]() { return stop_.load() || initFailed_ || !stubs_.empty(); });
        if (stop_.load() || stubs_.empty()) {
            return nullptr;
        }
        auto stub = std::move(stubs_.front());
        stubs_.pop();
        return stub;
    }

    void PutStub(std::unique_ptr<StubType> stub)
    {
        if (!stub) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stop_.load()) {
                stubs_.push(std::move(stub));
                cond_.notify_one();
                return;
            }
        }
    }

private:
    void Close()
    {
        stop_.store(true);
        cond_.notify_all();
    }

    std::atomic<bool> stop_;
    bool initFailed_;
    size_t poolSize_;
    std::string host_;
    std::string port_;
    std::queue<std::unique_ptr<StubType>> stubs_;
    std::condition_variable cond_;
    std::mutex mutex_;
};
} // namespace P1
#endif
