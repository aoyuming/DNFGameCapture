#include "../CloudMatchClient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void RequireEventually(const std::atomic<bool>& value, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!value.load(std::memory_order_acquire) &&
        std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(value.load(std::memory_order_acquire), message);
}

void TestDispatchRunsCallbackOnCallerAndAllowsStop()
{
    CloudMatchClient client;
    std::thread::id dispatchCaller;
    std::thread::id callbackCaller;
    std::string callbackMessage;
    std::atomic<bool> stopReturned{ false };
    std::size_t dispatched = 0;
    client.SetMessageCallback([&](std::string message) {
        callbackCaller = std::this_thread::get_id();
        callbackMessage = std::move(message);
        client.Stop();
        stopReturned.store(true, std::memory_order_release);
    });

    Require(!client.UploadSnapshot("{"),
        "invalid snapshot should synchronously report rejection");
    std::thread dispatcher([&]() {
        dispatchCaller = std::this_thread::get_id();
        dispatched = client.DispatchMessages();
    });
    dispatcher.join();

    Require(dispatched == 1, "DispatchMessages should drain the queued rejection once");
    Require(callbackCaller == dispatchCaller,
        "callback must run on the DispatchMessages caller thread");
    Require(stopReturned.load(std::memory_order_acquire),
        "callback should be able to call Stop without deadlock");
    Require(callbackMessage.find("snapshot_upload_result") != std::string::npos,
        "callback should receive the normalized snapshot rejection");
    Require(callbackMessage.find("invalid_payload") != std::string::npos,
        "invalid snapshot rejection should use the normalized code");
}

void TestConfigureDiscardsOldGenerationMessages()
{
    CloudMatchClient client;
    Require(!client.Configure(L"invalid-old-url", "old-device", "old-token"),
        "invalid old configuration should still advance generation");
    Require(!client.UploadSnapshot("{"), "old generation should queue one rejection");

    Require(!client.Configure(L"invalid-new-url", "new-device", "new-token"),
        "new invalid configuration should replace the old generation");
    Require(!client.UploadSnapshot("["), "new generation should queue one rejection");

    std::size_t callbacks = 0;
    std::string onlyMessage;
    client.SetMessageCallback([&](std::string message) {
        ++callbacks;
        onlyMessage = std::move(message);
    });
    Require(client.DispatchMessages(32) == 1,
        "Configure should discard queued messages from the old generation");
    Require(callbacks == 1, "only the new generation callback should be delivered");
    Require(onlyMessage.find("invalid_payload") != std::string::npos,
        "new generation rejection should remain available to the host");
    client.Stop();
}

void TestProtectedResultCapacityBackpressuresAndRecovers()
{
    CloudMatchClient client;
    client.ConfigureForTesting();

    std::size_t callbacks = 0;
    std::unordered_set<std::string> requestIds;
    client.SetMessageCallback([&](std::string message) {
        ++callbacks;
        for (std::size_t index = 0; index < 96; ++index) {
            const std::string requestId = "request-" + std::to_string(index);
            const std::string field = "\"requestId\":\"" + requestId + "\"";
            if (message.find(field) != std::string::npos) {
                requestIds.insert(requestId);
                break;
            }
        }
    });

    for (std::size_t index = 0; index < 48; ++index) {
        const std::string requestId = "request-" + std::to_string(index);
        Require(client.RequestComparison(requestId),
            "protected operation should be accepted while reserved capacity remains");
        Require(client.CompleteNextProtectedOperationForTesting(),
            "accepted operation should produce one protected result");
    }
    for (std::size_t index = 48; index < 96; ++index) {
        const std::string requestId = "request-" + std::to_string(index);
        Require(client.RequestComparison(requestId),
            "queued operations should retain protected result reservations");
    }

    Require(!client.RequestComparison("request-overflow"),
        "operation beyond protected result capacity must be rejected");
    Require(client.GetStatusSnapshot().statusText == "queue_full",
        "protected result backpressure should report queue_full");
    for (std::size_t index = 48; index < 96; ++index) {
        Require(client.CompleteNextProtectedOperationForTesting(),
            "each reserved operation should produce one protected result");
    }
    Require(client.DispatchMessages(128) == 96,
        "all accepted operations should retain exactly one protected result");
    Require(callbacks == 96 && requestIds.size() == 96,
        "all protected results should be delivered exactly once");

    Require(client.RequestComparison("request-recovered"),
        "dispatch should release protected capacity for another operation");
    Require(client.CompleteNextProtectedOperationForTesting(),
        "operation accepted after dispatch should complete");
    Require(client.DispatchMessages() == 1,
        "recovered protected capacity should deliver one result");
}

void TestDesiredRoomCannotCrossConfigurationGeneration()
{
    CloudMatchClient client;
    const std::uint64_t oldGeneration = client.ConfigureForTesting();
    Require(client.JoinRoom("old-room", "old-name"),
        "old-generation join should be accepted");

    const std::uint64_t newGeneration = client.ConfigureForTesting();
    Require(newGeneration != oldGeneration,
        "test configuration should advance generation");
    Require(!client.HasDesiredRoomForTesting(),
        "Configure should clear the old desired room");
    Require(!client.JoinRoomForGenerationForTesting(oldGeneration,
        "stale-room", "stale-name"),
        "an old-generation join must be rejected after Configure");
    Require(!client.HasDesiredRoomForTesting(),
        "a stale join must not restore the old desired room");

    Require(client.JoinRoomForGenerationForTesting(newGeneration,
        "new-room", "new-name"),
        "the active generation should still accept joins");
    Require(client.DesiredRoomGenerationForTesting() == newGeneration,
        "the remembered room should belong to the active generation");
}

void TestDispatchLifecycleGateWaitsAndRemainsReentrant()
{
    {
        CloudMatchClient client;
        std::mutex callbackMutex;
        std::condition_variable callbackCondition;
        bool releaseCallback = false;
        std::atomic<bool> callbackEntered{ false };
        std::atomic<bool> setterStarted{ false };
        std::atomic<bool> setterReturned{ false };

        client.SetMessageCallback([&](std::string) {
            callbackEntered.store(true, std::memory_order_release);
            std::unique_lock<std::mutex> lock(callbackMutex);
            callbackCondition.wait(lock, [&]() { return releaseCallback; });
        });
        Require(!client.UploadSnapshot("{"),
            "invalid snapshot should queue a callback for the gate test");

        std::thread dispatcher([&]() { client.DispatchMessages(1); });
        RequireEventually(callbackEntered, "blocking callback should start");
        std::thread setter([&]() {
            setterStarted.store(true, std::memory_order_release);
            client.SetMessageCallback([](std::string) {});
            setterReturned.store(true, std::memory_order_release);
        });
        RequireEventually(setterStarted, "callback setter thread should start");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Require(!setterReturned.load(std::memory_order_acquire),
            "SetMessageCallback must wait for an executing old callback");

        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            releaseCallback = true;
        }
        callbackCondition.notify_all();
        dispatcher.join();
        setter.join();
        Require(setterReturned.load(std::memory_order_acquire),
            "SetMessageCallback should return after the old callback exits");
    }

    {
        CloudMatchClient client;
        std::mutex callbackMutex;
        std::condition_variable callbackCondition;
        bool releaseCallback = false;
        std::atomic<bool> callbackEntered{ false };
        std::atomic<bool> stopStarted{ false };
        std::atomic<bool> stopReturned{ false };
        std::atomic<std::size_t> oldCallbackStarts{ 0 };

        client.SetMessageCallback([&](std::string) {
            oldCallbackStarts.fetch_add(1, std::memory_order_acq_rel);
            callbackEntered.store(true, std::memory_order_release);
            std::unique_lock<std::mutex> lock(callbackMutex);
            callbackCondition.wait(lock, [&]() { return releaseCallback; });
        });
        Require(!client.UploadSnapshot("{"), "first gate message should queue");
        Require(!client.UploadSnapshot("["), "second gate message should queue");

        std::thread dispatcher([&]() { client.DispatchMessages(1); });
        RequireEventually(callbackEntered, "blocking callback should start before Stop");
        std::thread stopper([&]() {
            stopStarted.store(true, std::memory_order_release);
            client.Stop();
            stopReturned.store(true, std::memory_order_release);
        });
        RequireEventually(stopStarted, "Stop thread should start");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        Require(!stopReturned.load(std::memory_order_acquire),
            "Stop must wait for an executing callback");

        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            releaseCallback = true;
        }
        callbackCondition.notify_all();
        dispatcher.join();
        stopper.join();
        Require(stopReturned.load(std::memory_order_acquire),
            "Stop should return after the callback exits");
        Require(client.DispatchMessages(8) == 0,
            "Stop should clear messages that could start an old callback");
        Require(oldCallbackStarts.load(std::memory_order_acquire) == 1,
            "an old callback must never start after Stop returns");
    }
}

void TestRememberedJoinRetriesAfterDispatchCapacityRelease()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    client.SetDesiredJoinForReplayForTesting("retry-room", "retry-name");

    for (std::size_t index = 0; index < 96; ++index) {
        Require(client.RequestComparison("capacity-" + std::to_string(index)),
            "capacity setup operation should be accepted");
        Require(client.CompleteNextProtectedOperationForTesting(),
            "capacity setup operation should complete");
    }

    Require(client.RetryRememberedJoinForTesting() == "no_capacity",
        "full protected capacity should defer remembered join without sending");
    Require(client.RememberedJoinSendCountForTesting() == 0,
        "capacity deferral must not send or reconnect");
    Require(client.DispatchMessages(1) == 1,
        "dispatch should free one protected result slot");
    Require(client.RetryRememberedJoinForTesting() == "sent",
        "remembered join should retry after dispatch releases capacity");
    Require(client.RememberedJoinSendCountForTesting() == 1,
        "remembered join should send exactly once after capacity recovery");
    Require(client.RetryRememberedJoinForTesting() == "already_in_flight",
        "in-flight remembered join should suppress duplicate ACK requests");
    Require(client.RememberedJoinSendCountForTesting() == 1,
        "duplicate retry must not send another remembered join");
}

} // namespace

int main()
{
    TestDispatchRunsCallbackOnCallerAndAllowsStop();
    TestConfigureDiscardsOldGenerationMessages();
    TestProtectedResultCapacityBackpressuresAndRecovers();
    TestDesiredRoomCannotCrossConfigurationGeneration();
    TestDispatchLifecycleGateWaitsAndRemainsReentrant();
    TestRememberedJoinRetriesAfterDispatchCapacityRelease();
    std::cout << "Cloud match client tests passed.\n";
    return 0;
}
