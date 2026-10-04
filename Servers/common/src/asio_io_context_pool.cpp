#include "chat/asio_io_context_pool.h"

#include "chat/log.h"

namespace P1 {

AsioIOContextPool::AsioIOContextPool(std::size_t size) : works_(size), poolSize_(size), nextIOContext_(0)
{
    ioContext_.reserve(size);
    works_.reserve(size);
    for (std::size_t i = 0; i < size; i++) {
        auto io = std::make_shared<boost::asio::io_context>();
        ioContext_.push_back(io);
        works_.push_back(std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            boost::asio::make_work_guard(io->get_executor())));
    }

    for (std::size_t i = 0; i < size; i++) {
        threads_.emplace_back([io = ioContext_[i]]() { io->run(); });
    }
    LOG_INFO("[AsioIOContextPool] constructed, pool size:%zu", size);
}
AsioIOContextPool::~AsioIOContextPool()
{
    Stop();
}

std::shared_ptr<boost::asio::io_context> AsioIOContextPool::GetIOService()
{
    auto idx = nextIOContext_.fetch_add(1, std::memory_order_relaxed) % poolSize_;
    return ioContext_[idx];
}
void AsioIOContextPool::Stop()
{
    // 1. 释放所有 work，让 io_context 不再阻塞
    for (auto& work : works_) {
        work.reset(); // 销毁 work 对象
    }

    // 2. 停止所有 io_context
    for (auto& io : ioContext_) {
        io->stop();
    }

    // 3. 等待所有线程退出
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}
} // namespace P1
