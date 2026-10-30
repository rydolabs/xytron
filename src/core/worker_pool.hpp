#ifndef NATIVEWEB_WORKER_POOL_HPP_INCLUDED
#define NATIVEWEB_WORKER_POOL_HPP_INCLUDED

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace nativeweb {
namespace detail {

typedef std::function<void()> WorkerTask;

enum class WorkerPoolShutdown
{
    Drain,
    CancelPending
};

class WorkerPool
{
public:
    explicit WorkerPool(
        std::size_t threadCount,
        std::size_t maxQueuedTasks = 0);

    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    // Returns false after shutdown begins or when the configured queue limit
    // has been reached. A zero maxQueuedTasks value means unbounded.
    bool post(const WorkerTask& task);

    // Running tasks are never force-terminated. Drain executes already queued
    // work; CancelPending drops work that has not started yet. Both modes wait
    // for currently running workers to exit.
    void shutdown(
        WorkerPoolShutdown mode =
            WorkerPoolShutdown::Drain);

    std::size_t threadCount() const;
    std::size_t queuedTaskCount() const;
    bool stopping() const;

private:
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<WorkerTask> tasks_;
    std::vector<std::thread> workers_;
    std::size_t maxQueuedTasks_;
    bool stopping_;
};

std::size_t defaultWorkerThreadCount();

// Process-wide default executor used by WebView bridge requests.
// Individual WebViews do not create their own native thread pools.
WorkerPool& defaultWorkerPool();

} // namespace detail
} // namespace nativeweb

#endif
