#ifndef _P1_ASIO_IO_CONTEXT_POOL_H_
#define _P1_ASIO_IO_CONTEXT_POOL_H_

#include <boost/asio.hpp>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "chat/noncopyable.h"
namespace P1 {
class AsioIOContextPool {
public:
    static AsioIOContextPool& GetInstance() { static AsioIOContextPool instance; return instance; }
    DISALLOW_COPY_MOVE(AsioIOContextPool);

    using IOContextPtr = std::shared_ptr<boost::asio::io_context>;
    // Work 保证io_context 在没有事件时不退出
    using Work = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
    using WorkPtr = std::unique_ptr<Work>;
    ~AsioIOContextPool();
    // 使用 round-robin 的方式返回一个 io_service
    std::shared_ptr<boost::asio::io_context> GetIOService();
    void Stop();

private:
    AsioIOContextPool(std::size_t size = 2 /*std::thread::hardware_concurrency()*/);
    std::vector<IOContextPtr> ioContext_;
    std::vector<WorkPtr> works_;
    std::vector<std::thread> threads_;
    std::size_t poolSize_;
    std::atomic<std::size_t> nextIOContext_;
};
} // namespace P1

#endif // _P1_ASIO_IO_CONTEXT_POOL_H_