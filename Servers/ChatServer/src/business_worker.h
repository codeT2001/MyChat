#ifndef _P1_BUSINESS_WORKER_H_
#define _P1_BUSINESS_WORKER_H_

#include <cstddef>
#include <cstdint>
#include <functional>

namespace P1 {

// 同一连接固定落到同一业务 worker，保证单连接消息按到达顺序处理；
// 不同连接之间允许并行。workerCount 为 0 时回退到 0，避免调用方除零。
inline std::size_t SelectBusinessWorker(uint32_t sessionId, std::size_t workerCount)
{
    if (workerCount <= 1) {
        return 0;
    }
    return std::hash<uint32_t> {}(sessionId) % workerCount;
}

} // namespace P1

#endif // _P1_BUSINESS_WORKER_H_
