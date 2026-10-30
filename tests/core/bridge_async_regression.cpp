#include "core/bridge_message.hpp"
#include "core/bridge_runtime.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
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

bool ready(
    std::future<Any>& future,
    int milliseconds = 3000)
{
    return future.wait_for(
        std::chrono::milliseconds(milliseconds)) ==
        std::future_status::ready;
}

void testRequestRunsOnWorker()
{
    nativeweb::detail::BridgeRuntime bridge;

    const std::thread::id caller =
        std::this_thread::get_id();

    std::promise<std::thread::id> executedPromise;
    std::future<std::thread::id> executed =
        executedPromise.get_future();

    bridge.bind(
        "thread.check",
        nativeweb::detail::makeDynamicFunction(
            [&executedPromise]() {
                executedPromise.set_value(
                    std::this_thread::get_id());
                return true;
            }));

    std::promise<Any> responsePromise;
    std::future<Any> response =
        responsePromise.get_future();

    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(
            1,
            "thread.check",
            VariantList()),
        [&responsePromise](const Any& value) {
            responsePromise.set_value(value);
        });

    CHECK(
        executed.wait_for(
            std::chrono::seconds(3)) ==
        std::future_status::ready);

    CHECK(executed.get() != caller);
    CHECK(ready(response));

    const nativeweb::detail::BridgeMessage parsed =
        nativeweb::detail::parseBridgeMessage(
            response.get());

    CHECK(
        parsed.type ==
        nativeweb::detail::BridgeMessageType::Response);
    CHECK(AnyCast<bool>(parsed.value));
}

void testConcurrentRequestsCanOverlap()
{
    nativeweb::detail::BridgeRuntime bridge;

    std::promise<void> releasePromise;
    std::shared_future<void> release =
        releasePromise.get_future().share();

    std::promise<void> slowStartedPromise;
    std::future<void> slowStarted =
        slowStartedPromise.get_future();

    bridge.bind(
        "slow",
        nativeweb::detail::makeDynamicFunction(
            [&]() {
                slowStartedPromise.set_value();
                release.wait();
                return 1;
            }));

    bridge.bind(
        "fast",
        nativeweb::detail::makeDynamicFunction(
            []() {
                return 2;
            }));

    std::promise<Any> slowResponsePromise;
    std::future<Any> slowResponse =
        slowResponsePromise.get_future();

    std::promise<Any> fastResponsePromise;
    std::future<Any> fastResponse =
        fastResponsePromise.get_future();

    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(
            10,
            "slow",
            VariantList()),
        [&slowResponsePromise](const Any& value) {
            slowResponsePromise.set_value(value);
        });

    CHECK(
        slowStarted.wait_for(
            std::chrono::seconds(3)) ==
        std::future_status::ready);

    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(
            11,
            "fast",
            VariantList()),
        [&fastResponsePromise](const Any& value) {
            fastResponsePromise.set_value(value);
        });

    // Fast must finish while slow is still blocked. This proves that incoming
    // requests are not serialized through one worker.
    CHECK(ready(fastResponse, 1500));

    const nativeweb::detail::BridgeMessage fastParsed =
        nativeweb::detail::parseBridgeMessage(
            fastResponse.get());

    CHECK(fastParsed.requestId == 11u);
    CHECK(AnyCast<int>(fastParsed.value) == 2);

    releasePromise.set_value();

    CHECK(ready(slowResponse));

    const nativeweb::detail::BridgeMessage slowParsed =
        nativeweb::detail::parseBridgeMessage(
            slowResponse.get());

    CHECK(slowParsed.requestId == 10u);
    CHECK(AnyCast<int>(slowParsed.value) == 1);
}

void testExceptionRejectsOnlyMatchingRequest()
{
    nativeweb::detail::BridgeRuntime bridge;

    bridge.bind(
        "fail",
        nativeweb::detail::makeDynamicFunction(
            []() -> int {
                throw std::runtime_error("expected failure");
            }));

    std::promise<Any> responsePromise;
    std::future<Any> response =
        responsePromise.get_future();

    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(
            20,
            "fail",
            VariantList()),
        [&responsePromise](const Any& value) {
            responsePromise.set_value(value);
        });

    CHECK(ready(response));

    const nativeweb::detail::BridgeMessage parsed =
        nativeweb::detail::parseBridgeMessage(
            response.get());

    CHECK(
        parsed.type ==
        nativeweb::detail::BridgeMessageType::Error);
    CHECK(parsed.requestId == 20u);
    CHECK(parsed.errorCode == "native_exception");
}

void testStoppedRuntimeRejectsNewRequest()
{
    nativeweb::detail::BridgeRuntime bridge;

    bridge.bind(
        "value",
        nativeweb::detail::makeDynamicFunction(
            []() { return 1; }));

    bridge.shutdown();

    std::promise<Any> responsePromise;
    std::future<Any> response =
        responsePromise.get_future();

    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(
            30,
            "value",
            VariantList()),
        [&responsePromise](const Any& value) {
            responsePromise.set_value(value);
        });

    CHECK(ready(response));

    const nativeweb::detail::BridgeMessage parsed =
        nativeweb::detail::parseBridgeMessage(
            response.get());

    CHECK(parsed.errorCode == "runtime_stopped");
}

void testCancellationSuppressesLateResult()
{
    nativeweb::detail::BridgeRuntime bridge;
    std::promise<void> startedPromise;
    std::future<void> started=startedPromise.get_future();
    std::promise<void> releasePromise;
    std::shared_future<void> release=releasePromise.get_future().share();
    std::promise<void> finishedPromise;
    std::future<void> finished=finishedPromise.get_future();
    std::atomic<int> delivered(0);
    bridge.bind("blocking",nativeweb::detail::makeDynamicFunction([&]() {
        startedPromise.set_value();
        release.wait();
        finishedPromise.set_value();
        return 99;
    }));
    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(440,"blocking",VariantList()),
        [&](const Any&) { ++delivered; });
    CHECK(started.wait_for(std::chrono::seconds(3))==std::future_status::ready);
    CHECK(bridge.incomingCount()==1u);
    bridge.receive(nativeweb::detail::makeCancelMessage(440));
    CHECK(bridge.incomingCount()==0u);
    CHECK(!bridge.cancelIncoming(440));
    releasePromise.set_value();
    CHECK(finished.wait_for(std::chrono::seconds(3))==std::future_status::ready);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    CHECK(delivered.load()==0);

    bridge.bind("fast",nativeweb::detail::makeDynamicFunction([](){return 42;}));
    std::promise<Any> responsePromise;
    std::future<Any> response=responsePromise.get_future();
    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(441,"fast",VariantList()),
        [&](const Any& value){responsePromise.set_value(value);});
    CHECK(ready(response));
    if(ready(response,0)) {
        const nativeweb::detail::BridgeMessage result=
            nativeweb::detail::parseBridgeMessage(response.get());
        CHECK(result.requestId==441u);
        CHECK(AnyCast<int>(result.value)==42);
    }
    CHECK(bridge.incomingCount()==0u);
}


void testDuplicateCompletionCanReenterRuntime()
{
    nativeweb::detail::BridgeRuntime bridge;
    std::promise<void> startedPromise;
    std::future<void> started=startedPromise.get_future();
    std::promise<void> releasePromise;
    std::shared_future<void> release=releasePromise.get_future().share();
    bridge.bind("hold",nativeweb::detail::makeDynamicFunction([&]() {
        startedPromise.set_value();
        release.wait();
        return 17;
    }));

    std::promise<Any> originalPromise;
    std::future<Any> original=originalPromise.get_future();
    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(610,"hold",VariantList()),
        [&](const Any& value){originalPromise.set_value(value);});
    CHECK(started.wait_for(std::chrono::seconds(3))==std::future_status::ready);

    std::promise<Any> duplicatePromise;
    std::future<Any> duplicate=duplicatePromise.get_future();
    std::atomic<std::size_t> observed(0);
    // This completion must not run under AsyncDispatchState::mutex.
    // The old implementation deadlocked when a callback queried the map.
    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(610,"hold",VariantList()),
        [&](const Any& value){
            observed.store(bridge.incomingCount());
            duplicatePromise.set_value(value);
        });
    CHECK(ready(duplicate));
    if(ready(duplicate,0))
    {
        const nativeweb::detail::BridgeMessage parsed=
            nativeweb::detail::parseBridgeMessage(duplicate.get());
        CHECK(parsed.requestId==610u);
        CHECK(parsed.type==nativeweb::detail::BridgeMessageType::Error);
        CHECK(parsed.errorCode=="duplicate_request");
    }
    CHECK(observed.load()==1u);
    CHECK(bridge.incomingCount()==1u);
    releasePromise.set_value();
    CHECK(ready(original));
    if(ready(original,0))
    {
        const nativeweb::detail::BridgeMessage parsed=
            nativeweb::detail::parseBridgeMessage(original.get());
        CHECK(parsed.type==nativeweb::detail::BridgeMessageType::Response);
        CHECK(AnyCast<int>(parsed.value)==17);
    }
    CHECK(bridge.incomingCount()==0u);
}

void testCancelledRequestIdReuseDoesNotDeliverOldCompletion()
{
    nativeweb::detail::BridgeRuntime bridge;
    std::promise<void> oldStartedPromise, newStartedPromise;
    std::future<void> oldStarted=oldStartedPromise.get_future();
    std::future<void> newStarted=newStartedPromise.get_future();
    std::promise<void> oldReleasePromise, newReleasePromise;
    std::shared_future<void> oldRelease=oldReleasePromise.get_future().share();
    std::shared_future<void> newRelease=newReleasePromise.get_future().share();
    std::promise<void> oldFinishedPromise;
    std::future<void> oldFinished=oldFinishedPromise.get_future();
    std::atomic<int> oldCallbacks(0);
    std::atomic<int> newCallbacks(0);
    bridge.bind("old",nativeweb::detail::makeDynamicFunction([&]() {
        oldStartedPromise.set_value();
        oldRelease.wait();
        oldFinishedPromise.set_value();
        return 111;
    }));
    bridge.bind("new",nativeweb::detail::makeDynamicFunction([&]() {
        newStartedPromise.set_value();
        newRelease.wait();
        return 222;
    }));

    const nativeweb::detail::RequestId reusedId=702;
    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(reusedId,"old",VariantList()),
        [&](const Any&){++oldCallbacks;});
    CHECK(oldStarted.wait_for(std::chrono::seconds(3))==std::future_status::ready);
    CHECK(bridge.cancelIncoming(reusedId));
    CHECK(bridge.incomingCount()==0u);

    std::promise<Any> newResponsePromise;
    std::future<Any> newResponse=newResponsePromise.get_future();
    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(reusedId,"new",VariantList()),
        [&](const Any& value){
            ++newCallbacks;
            newResponsePromise.set_value(value);
        });
    CHECK(newStarted.wait_for(std::chrono::seconds(3))==std::future_status::ready);
    oldReleasePromise.set_value();
    CHECK(oldFinished.wait_for(std::chrono::seconds(3))==std::future_status::ready);
    // The old task must not remove or answer the new task with the same ID.
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    CHECK(oldCallbacks.load()==0);
    CHECK(bridge.incomingCount()==1u);

    newReleasePromise.set_value();
    CHECK(ready(newResponse));
    if(ready(newResponse,0))
    {
        const nativeweb::detail::BridgeMessage parsed=
            nativeweb::detail::parseBridgeMessage(newResponse.get());
        CHECK(parsed.requestId==reusedId);
        CHECK(parsed.type==nativeweb::detail::BridgeMessageType::Response);
        CHECK(AnyCast<int>(parsed.value)==222);
    }
    CHECK(newCallbacks.load()==1);
    CHECK(bridge.incomingCount()==0u);
}

void testShutdownSuppressesInFlightCompletion()
{
    std::atomic<int> delivered(0);
    std::promise<void> startedPromise, finishedPromise, releasePromise;
    std::future<void> started=startedPromise.get_future();
    std::future<void> finished=finishedPromise.get_future();
    std::shared_future<void> release=releasePromise.get_future().share();
    std::unique_ptr<nativeweb::detail::BridgeRuntime> bridge(
        new nativeweb::detail::BridgeRuntime());
    bridge->bind("long.running",nativeweb::detail::makeDynamicFunction([&]() {
        startedPromise.set_value();
        release.wait();
        finishedPromise.set_value();
        return 55;
    }));
    bridge->receiveAsync(
        nativeweb::detail::makeRequestMessage(801,"long.running",VariantList()),
        [&](const Any&){++delivered;});
    CHECK(started.wait_for(std::chrono::seconds(3))==std::future_status::ready);
    bridge->shutdown();
    CHECK(bridge->incomingCount()==0u);
    bridge.reset();
    releasePromise.set_value();
    CHECK(finished.wait_for(std::chrono::seconds(3))==std::future_status::ready);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    CHECK(delivered.load()==0);
}

void testCancellationOnNavigation()
{
    nativeweb::detail::BridgeRuntime bridge;
    std::promise<void> startedPromise;
    std::future<void> started=startedPromise.get_future();
    std::promise<void> releasePromise;
    std::shared_future<void> release=releasePromise.get_future().share();
    std::atomic<int> callbacks(0);
    bridge.bind("wait",nativeweb::detail::makeDynamicFunction([&](){
        startedPromise.set_value();
        release.wait();
        return true;
    }));
    bridge.receiveAsync(
        nativeweb::detail::makeRequestMessage(501,"wait",VariantList()),
        [&](const Any&){++callbacks;});
    CHECK(started.wait_for(std::chrono::seconds(3))==std::future_status::ready);
    bridge.cancelAllIncoming();
    CHECK(bridge.incomingCount()==0u);
    releasePromise.set_value();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    CHECK(callbacks.load()==0);
}

// Repeated cancellation must never poison an unrelated in-flight call.
// Use a gate rather than sleeps to make the cancellation happen while the
// cancelled worker is actually executing.
void testCancellationIsolationRepeated()
{
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        nativeweb::detail::BridgeRuntime bridge;
        std::promise<void> startedPromise;
        std::future<void> started = startedPromise.get_future();
        std::promise<void> releasePromise;
        std::shared_future<void> release = releasePromise.get_future().share();
        std::promise<void> finishedPromise;
        std::future<void> finished = finishedPromise.get_future();
        std::atomic<int> cancelledCallbacks(0);

        bridge.bind("blocked", nativeweb::detail::makeDynamicFunction([&]() {
            startedPromise.set_value();
            release.wait();
            finishedPromise.set_value();
            return 99;
        }));
        bridge.bind("healthy", nativeweb::detail::makeDynamicFunction([]() {
            return 17;
        }));

        const nativeweb::detail::RequestId blockedId =
            static_cast<nativeweb::detail::RequestId>(1000 + iteration * 2);
        const nativeweb::detail::RequestId healthyId = blockedId + 1;

        bridge.receiveAsync(
            nativeweb::detail::makeRequestMessage(
                blockedId, "blocked", VariantList()),
            [&](const Any&) { ++cancelledCallbacks; });

        const bool didStart =
            started.wait_for(std::chrono::seconds(3)) ==
            std::future_status::ready;
        CHECK(didStart);
        if (!didStart)
        {
            // Avoid waiting forever in a broken implementation.
            releasePromise.set_value();
            continue;
        }

        bridge.receive(nativeweb::detail::makeCancelMessage(blockedId));
        bridge.receive(nativeweb::detail::makeCancelMessage(blockedId));
        // Cancelling an unrelated, nonexistent ID must be harmless.
        bridge.receive(nativeweb::detail::makeCancelMessage(healthyId + 100000));

        std::promise<Any> healthyPromise;
        std::future<Any> healthy = healthyPromise.get_future();
        bridge.receiveAsync(
            nativeweb::detail::makeRequestMessage(
                healthyId, "healthy", VariantList()),
            [&](const Any& value) { healthyPromise.set_value(value); });

        const bool healthyReady = ready(healthy);
        CHECK(healthyReady);
        if (healthyReady)
        {
            const nativeweb::detail::BridgeMessage parsed =
                nativeweb::detail::parseBridgeMessage(healthy.get());
            CHECK(parsed.requestId == healthyId);
            CHECK(parsed.type ==
                  nativeweb::detail::BridgeMessageType::Response);
            CHECK(AnyCast<int>(parsed.value) == 17);
        }

        releasePromise.set_value();
        CHECK(finished.wait_for(std::chrono::seconds(3)) ==
              std::future_status::ready);
        // The worker has finished, but async delivery may still be scheduled;
        // shutdown joins the runtime and drains the outstanding dispatch.
        bridge.shutdown();
        CHECK(cancelledCallbacks.load() == 0);
        CHECK(bridge.incomingCount() == 0u);
    }
}

} // namespace

int main()
{
    testRequestRunsOnWorker();
    testConcurrentRequestsCanOverlap();
    testExceptionRejectsOnlyMatchingRequest();
    testStoppedRuntimeRejectsNewRequest();
    testCancellationSuppressesLateResult();
    testCancellationOnNavigation();
    testCancellationIsolationRepeated();
    testDuplicateCompletionCanReenterRuntime();
    testCancelledRequestIdReuseDoesNotDeliverOldCompletion();
    testShutdownSuppressesInFlightCompletion();

    if (failures != 0)
    {
        std::cerr
            << "Async bridge regression failed: "
            << failures
            << " check(s)"
            << std::endl;
        return 1;
    }

    std::cout
        << "Async bridge regression: PASS"
        << std::endl;
    return 0;
}
