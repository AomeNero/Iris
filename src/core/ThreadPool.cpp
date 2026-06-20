// Iris Core —— 线程池实现
#include "core/ThreadPool.h"

namespace iris {

ThreadPool::ThreadPool(std::size_t numThreads) {
    if (numThreads == 0) numThreads = 1;
    workers_.reserve(numThreads);
    for (std::size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex_);
                    condition_.wait(lock, [this] {
                        return stop_.load() || !tasks_.empty();
                    });
                    if (stop_.load() && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true);
    condition_.notify_all();
    for (auto& w : workers_)
        if (w.joinable()) w.join();
}

} // namespace iris
