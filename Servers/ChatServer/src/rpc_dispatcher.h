#ifndef _P1_RPC_DISPATCHER_H_
#define _P1_RPC_DISPATCHER_H_

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "chat/noncopyable.h"

namespace P1 {

// 同一个 key 固定落到同一个 worker，保证同一目标节点的通知顺序。
inline std::size_t SelectRpcWorker(const std::string& key, std::size_t workerCount)
{
    if (workerCount <= 1) {
        return 0;
    }
    return std::hash<std::string> {}(key) % workerCount;
}

// 跨节点 gRPC 派发器：把同步 RPC 从 LogicSystem 单业务线程移到固定 worker 上，
// 避免某个对端卡住时拖住本节点所有业务消息。队列满时丢弃并记录日志，
// 保持 fire-and-forget 语义（客户端 ACK 仍然由本地 Handler 立即返回）。
class RpcDispatcher {
public:
    static RpcDispatcher& GetInstance();
    DISALLOW_COPY_MOVE(RpcDispatcher);
    ~RpcDispatcher();

    bool Post(const std::string& key, std::function<void()> task);
    void Stop();

private:
    struct Worker {
        std::mutex mutex;
        std::condition_variable cond;
        std::queue<std::function<void()>> tasks;
    };

    RpcDispatcher();
    void WorkerLoop(std::size_t index);

    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::thread> threads_;
    std::size_t maxPendingPerWorker_ = 4096;
    std::atomic<bool> stop_ { false };
};

} // namespace P1

#endif // _P1_RPC_DISPATCHER_H_
