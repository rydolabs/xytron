#include "core/worker_pool.hpp"

#include <algorithm>
#include <stdexcept>

namespace nativeweb {
namespace detail {

std::size_t defaultWorkerThreadCount()
{
    const unsigned int detected =
        std::thread::hardware_concurrency();

    if (detected == 0)
        return 2;

    const std::size_t count =
        static_cast<std::size_t>(detected);

    return std::max<std::size_t>(
        2,
        std::min<std::size_t>(count, 8));
}

WorkerPool::WorkerPool(
    std::size_t threadCount,
    std::size_t maxQueuedTasks)
    : maxQueuedTasks_(maxQueuedTasks),
      stopping_(false)
{
    if (threadCount == 0)
        throw std::invalid_argument(
            "NativeWeb WorkerPool requires at least one thread");

    workers_.reserve(threadCount);

    try
    {
        for (std::size_t i = 0; i < threadCount; ++i)
        {
            workers_.push_back(
                std::thread(
                    &WorkerPool::workerLoop,
                    this));
        }
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }

        condition_.notify_all();

        for (std::size_t i = 0; i < workers_.size(); ++i)
        {
            if (workers_[i].joinable())
                workers_[i].join();
        }

        throw;
    }
}

WorkerPool::~WorkerPool()
{
    shutdown(WorkerPoolShutdown::CancelPending);
}

bool WorkerPool::post(const WorkerTask& task)
{
    if (!task)
        return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopping_)
            return false;

        if (maxQueuedTasks_ != 0 &&
            tasks_.size() >= maxQueuedTasks_)
        {
            return false;
        }

        tasks_.push_back(task);
    }

    condition_.notify_one();
    return true;
}

void WorkerPool::shutdown(
    WorkerPoolShutdown mode)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!stopping_)
        {
            stopping_ = true;

            if (mode == WorkerPoolShutdown::CancelPending)
                tasks_.clear();
        }
        else if (
            mode == WorkerPoolShutdown::CancelPending)
        {
            tasks_.clear();
        }
    }

    condition_.notify_all();

    for (std::size_t i = 0; i < workers_.size(); ++i)
    {
        if (workers_[i].joinable())
            workers_[i].join();
    }

    workers_.clear();
}

std::size_t WorkerPool::threadCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

std::size_t WorkerPool::queuedTaskCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

bool WorkerPool::stopping() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stopping_;
}

void WorkerPool::workerLoop()
{
    for (;;)
    {
        WorkerTask task;

        {
            std::unique_lock<std::mutex> lock(mutex_);

            condition_.wait(
                lock,
                [this]() {
                    return stopping_ ||
                        !tasks_.empty();
                });

            if (tasks_.empty())
            {
                if (stopping_)
                    return;

                continue;
            }

            task = tasks_.front();
            tasks_.pop_front();
        }

        try
        {
            task();
        }
        catch (...)
        {
            // A user callable must not kill a runtime worker. The bridge layer
            // is responsible for translating callable exceptions to errors.
        }
    }
}

WorkerPool& defaultWorkerPool()
{
    // A bounded process-wide queue prevents an accidental JavaScript request
    // storm from growing memory without limit. Public tuning comes later.
    static WorkerPool pool(
        defaultWorkerThreadCount(),
        4096);

    return pool;
}

} // namespace detail
} // namespace nativeweb
