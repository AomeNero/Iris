// Iris Core —— 线程池
// 设计依据: doc/detailed-design.md §5.4
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace iris {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// 提交任务，返回 future
    template <typename Func>
    auto Enqueue(Func&& f) -> std::future<decltype(f())>;

    /// 提交可取消任务。cancelled 为外部原子标记，任务应定期检查。
    template <typename Func>
    auto EnqueueCancellable(Func&& f, std::shared_ptr<std::atomic<bool>> cancelled)
        -> std::future<decltype(f())>;

    std::size_t ThreadCount() const { return workers_.size(); }

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        queueMutex_;
    std::condition_variable           condition_;
    std::atomic<bool>                 stop_{false};
};

template <typename Func>
auto ThreadPool::Enqueue(Func&& f) -> std::future<decltype(f())> {
    using R = decltype(f());
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<Func>(f));
    std::future<R> res = task->get_future();
    {
        std::lock_guard lock(queueMutex_);
        tasks_.emplace([task]() { (*task)(); });
    }
    condition_.notify_one();
    return res;
}

template <typename Func>
auto ThreadPool::EnqueueCancellable(Func&& f,
                                    std::shared_ptr<std::atomic<bool>> /*cancelled*/)
    -> std::future<decltype(f())> {
    // cancelled 由调用方在 f 内部按需检查（通过值捕获的 shared_ptr）。
    // 此处仅提交任务，保留 token 参数以匹配接口约定并提示语义。
    return Enqueue(std::forward<Func>(f));
}

} // namespace iris
