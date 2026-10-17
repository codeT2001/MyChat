#include "rpc_dispatcher.h"

#include <algorithm>
#include <exception>

#include "chat/config_manager.h"
#include "chat/log.h"

namespace P1 {
namespace {
constexpr int32_t DEFAULT_WORKER_THREADS = 2;
constexpr int32_t MAX_WORKER_THREADS = 16;
constexpr int32_t DEFAULT_MAX_PENDING_PER_WORKER = 4096;
} // namespace

RpcDispatcher& RpcDispatcher::GetInstance()
{
    static RpcDispatcher instance;
    return instance;
}

RpcDispatcher::RpcDispatcher()
{
    auto& cfg = ConfigManager::GetInstance();
    int32_t workerCount = cfg.GetValue<int32_t>("RPC", "WorkerThreads", DEFAULT_WORKER_THREADS);
    workerCount = std::clamp(workerCount, 1, MAX_WORKER_THREADS);

    int32_t maxPending = cfg.GetValue<int32_t>("RPC", "MaxPendingPerWorker", DEFAULT_MAX_PENDING_PER_WORKER);
    maxPendingPerWorker_ = static_cast<std::size_t>(std::max(maxPending, 1));

    workers_.reserve(static_cast<std::size_t>(workerCount));
    threads_.reserve(static_cast<std::size_t>(workerCount));
    for (int32_t i = 0; i < workerCount; ++i) {
        workers_.push_back(std::make_unique<Worker>());
    }
    for (std::size_t i = 0; i < workers_.size(); ++i) {
        threads_.emplace_back(&RpcDispatcher::WorkerLoop, this, i);
    }
    LOG_INFO("[RpcDispatcher] started, workers:%d, maxPendingPerWorker:%zu",
        workerCount, maxPendingPerWorker_);
}

RpcDispatcher::~RpcDispatcher()
{
    Stop();
}

bool RpcDispatcher::Post(const std::string& key, std::function<void()> task)
{
    if (stop_.load() || !task) {
        return false;
    }
    if (workers_.empty()) {
        return false;
    }

    Worker& worker = *workers_[SelectRpcWorker(key, workers_.size())];
    {
        std::lock_guard<std::mutex> lock(worker.mutex);
        if (worker.tasks.size() >= maxPendingPerWorker_) {
            LOG_ERROR("[RpcDispatcher] queue full, drop task, key:%s, pending:%zu", key.c_str(), worker.tasks.size());
            return false;
        }
        worker.tasks.push(std::move(task));
    }
    worker.cond.notify_one();
    return true;
}

void RpcDispatcher::Stop()
{
    if (stop_.exchange(true)) {
        return;
    }
    for (auto& worker : workers_) {
        worker->cond.notify_all();
    }
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    LOG_INFO("[RpcDispatcher] stopped");
}

void RpcDispatcher::WorkerLoop(std::size_t index)
{
    Worker& worker = *workers_[index];
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(worker.mutex);
            worker.cond.wait(lock, [this, &worker]() { return stop_.load() || !worker.tasks.empty(); });
            if (stop_.load()) {
                return;
            }
            task = std::move(worker.tasks.front());
            worker.tasks.pop();
        }
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR("[RpcDispatcher] task exception, worker:%zu, error:%s", index, e.what());
        } catch (...) {
            LOG_ERROR("[RpcDispatcher] task unknown exception, worker:%zu", index);
        }
    }
}

} // namespace P1
