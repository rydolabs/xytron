#include "core/worker_pool.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

int failures = 0;

void fail(
    const char* expression,
    const char* file,
    int line)
{
    std::cerr
        << file << ":" << line
        << ": CHECK failed: "
        << expression
        << std::endl;
    ++failures;
}

#define CHECK(expr)     do { if (!(expr)) fail(#expr, __FILE__, __LINE__); } while (0)

bool readyWithin(
    std::future<void>& future,
    int milliseconds = 3000)
{
    return future.wait_for(
        std::chrono::milliseconds(milliseconds)) ==
        std::future_status::ready;
}

void testRunsOffCallerThread()
{
    nativeweb::detail::WorkerPool pool(1);

    const std::thread::id caller =
        std::this_thread::get_id();

    std::promise<std::thread::id> promise;
    std::future<std::thread::id> future =
        promise.get_future();

    CHECK(pool.post([&promise]() {
        promise.set_value(
            std::this_thread::get_id());
    }));

    CHECK(
        future.wait_for(
            std::chrono::seconds(3)) ==
        std::future_status::ready);

    CHECK(future.get() != caller);
    pool.shutdown();
}

void testTwoWorkersActuallyOverlap()
{
    nativeweb::detail::WorkerPool pool(2);

    std::promise<void> releasePromise;
    std::shared_future<void> release =
        releasePromise.get_future().share();

    std::promise<void> firstStartedPromise;
    std::promise<void> secondStartedPromise;

    std::future<void> firstStarted =
        firstStartedPromise.get_future();
    std::future<void> secondStarted =
        secondStartedPromise.get_future();

    CHECK(pool.post([&]() {
        firstStartedPromise.set_value();
        release.wait();
    }));

    CHECK(pool.post([&]() {
        secondStartedPromise.set_value();
        release.wait();
    }));

    const bool firstReady =
        readyWithin(firstStarted);
    const bool secondReady =
        readyWithin(secondStarted);

    CHECK(firstReady);
    CHECK(secondReady);

    releasePromise.set_value();
    pool.shutdown();
}

void testTaskExceptionDoesNotKillWorker()
{
    nativeweb::detail::WorkerPool pool(1);

    std::promise<void> completedPromise;
    std::future<void> completed =
        completedPromise.get_future();

    CHECK(pool.post([]() {
        throw std::runtime_error(
            "expected worker task failure");
    }));

    CHECK(pool.post([&]() {
        completedPromise.set_value();
    }));

    CHECK(readyWithin(completed));
    pool.shutdown();
}

void testQueueLimit()
{
    nativeweb::detail::WorkerPool pool(
        1,
        1);

    std::promise<void> releasePromise;
    std::shared_future<void> release =
        releasePromise.get_future().share();

    std::promise<void> runningPromise;
    std::future<void> running =
        runningPromise.get_future();

    CHECK(pool.post([&]() {
        runningPromise.set_value();
        release.wait();
    }));

    CHECK(readyWithin(running));

    // Worker is occupied. The first queued task consumes the only queue slot.
    CHECK(pool.post([]() {}));
    CHECK(!pool.post([]() {}));

    releasePromise.set_value();
    pool.shutdown();
}

void testShutdownRejectsNewWork()
{
    nativeweb::detail::WorkerPool pool(1);

    pool.shutdown(
        nativeweb::detail::WorkerPoolShutdown::CancelPending);

    CHECK(pool.stopping());
    CHECK(!pool.post([]() {}));
}

void testDefaultThreadCountIsBounded()
{
    const std::size_t count =
        nativeweb::detail::defaultWorkerThreadCount();

    CHECK(count >= 2u);
    CHECK(count <= 8u);
}

} // namespace

int main()
{
    testRunsOffCallerThread();
    testTwoWorkersActuallyOverlap();
    testTaskExceptionDoesNotKillWorker();
    testQueueLimit();
    testShutdownRejectsNewWork();
    testDefaultThreadCountIsBounded();

    if (failures != 0)
    {
        std::cerr
            << "Worker pool regression failed: "
            << failures
            << " check(s)"
            << std::endl;
        return 1;
    }

    std::cout
        << "Worker pool regression: PASS"
        << std::endl;
    return 0;
}
