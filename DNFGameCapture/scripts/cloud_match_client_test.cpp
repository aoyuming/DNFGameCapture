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
#include <vector>

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

std::string MakeSnapshot(std::uint64_t clientRevision, std::size_t padding = 0)
{
    return "{\"clientRevision\":" + std::to_string(clientRevision) +
        ",\"padding\":\"" + std::string(padding, 'x') + "\"}";
}

bool HasRevision(const std::string& message, std::uint64_t clientRevision)
{
    return message.find("\"clientRevision\":" + std::to_string(clientRevision)) !=
        std::string::npos;
}

void TestSnapshotResultsCarryLocalRevisionAndFilterOldGeneration()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    std::vector<std::string> messages;
    client.SetMessageCallback([&](std::string message) {
        messages.push_back(std::move(message));
    });

    Require(client.UploadSnapshot(MakeSnapshot(101)),
        "snapshot A should enter the latest offline slot");
    Require(client.UploadSnapshot(MakeSnapshot(303)),
        "snapshot C should replace snapshot A");
    Require(client.DispatchMessages(8) == 1,
        "replacing snapshot A should emit one cancellation result");
    Require(messages.back().find("\"code\":\"canceled\"") != std::string::npos &&
        HasRevision(messages.back(), 101),
        "canceled snapshot A must retain revision 101");

    Require(client.CompleteLatestSnapshotAckForTesting(true, 303, {}),
        "snapshot C should be completed through the ACK path");
    Require(client.DispatchMessages(8) == 1,
        "snapshot C success should emit one result");
    Require(messages.back().find("\"acceptedRevision\":303") != std::string::npos &&
        HasRevision(messages.back(), 303),
        "snapshot C success must retain revision 303");

    Require(client.UploadSnapshot(MakeSnapshot(404)),
        "snapshot timeout fixture should enter the latest slot");
    Require(client.ExpireLatestSnapshotAckForTesting(),
        "snapshot timeout should run through the production expiry path");
    Require(client.DispatchMessages(8) == 1,
        "snapshot timeout should emit one result");
    Require(messages.back().find("\"code\":\"timeout\"") != std::string::npos &&
        HasRevision(messages.back(), 404),
        "expired snapshot ACK must retain revision 404");

    Require(client.UploadSnapshot(MakeSnapshot(405)),
        "snapshot disconnect fixture should enter the latest slot");
    Require(client.FailLatestSnapshotAckForTesting("connection_lost"),
        "snapshot disconnect should run through the production failure path");
    Require(client.DispatchMessages(8) == 1,
        "snapshot disconnect should emit one result");
    Require(messages.back().find("\"code\":\"connection_lost\"") != std::string::npos &&
        HasRevision(messages.back(), 405),
        "connection-lost snapshot ACK must retain revision 405");

    Require(!client.UploadSnapshot(MakeSnapshot(505, 70000)),
        "oversized legal JSON should fail before entering the offline slot");
    Require(client.DispatchMessages(8) == 1 && HasRevision(messages.back(), 505),
        "encoding failure must retain revision 505");

    Require(!client.UploadSnapshot(MakeSnapshot(606, 70000)),
        "old-generation encoding failure should be queued");
    client.ConfigureForTesting();
    Require(!client.UploadSnapshot(MakeSnapshot(707, 70000)),
        "new-generation encoding failure should be queued");
    Require(client.DispatchMessages(8) == 1,
        "Configure should discard the old-generation snapshot result");
    Require(HasRevision(messages.back(), 707) && !HasRevision(messages.back(), 606),
        "only the current-generation snapshot result may be dispatched");
}

void TestOversizedProtectedFallbackPreservesCorrelation()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    std::vector<std::string> messages;
    client.SetMessageCallback([&](std::string message) {
        messages.push_back(std::move(message));
    });

    Require(client.UploadSnapshot(MakeSnapshot(808)),
        "oversized ACK fixture should enter the latest slot");
    Require(client.CompleteLatestSnapshotAckForTesting(true, 808, {}, 140000),
        "oversized snapshot ACK should run through HandleAck and NotifyJson");
    Require(client.DispatchMessages(8) == 1,
        "oversized snapshot ACK should emit a compact fallback result");
    Require(messages.back().find("\"code\":\"payload_too_large\"") != std::string::npos &&
        HasRevision(messages.back(), 808),
        "oversized snapshot fallback must preserve clientRevision 808");

    Require(client.RequestComparison("comparison-request-42"),
        "oversized request fixture should enter the command queue");
    Require(client.CompleteNextProtectedOperationForTesting(140000),
        "oversized request result should run through NotifyJson");
    Require(client.DispatchMessages(8) == 1,
        "oversized request result should emit a compact fallback result");
    Require(messages.back().find("\"code\":\"payload_too_large\"") != std::string::npos &&
        messages.back().find("\"requestId\":\"comparison-request-42\"") != std::string::npos,
        "oversized protected request fallback must preserve requestId");
}

void TestComparisonPaginationCarriesImmutableToken()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    std::vector<std::string> messages;
    client.SetMessageCallback([&](std::string message) {
        messages.push_back(std::move(message));
    });

    const std::string token = "abcdefghijklmnopqrstuvwxyzABCDEF";
    Require(client.RequestComparison("comparison-page-2", "member-cursor-0008",
        8, token), "comparison continuation should accept a bounded token");
    Require(client.CompleteNextProtectedOperationForTesting(),
        "comparison continuation should preserve queued correlation fields");
    Require(client.DispatchMessages(4) == 1,
        "comparison continuation should dispatch one result");
    Require(messages.back().find("\"requestId\":\"comparison-page-2\"") !=
        std::string::npos &&
        messages.back().find("\"cursor\":\"member-cursor-0008\"") !=
        std::string::npos &&
        messages.back().find("\"comparisonToken\":\"" + token + "\"") !=
        std::string::npos,
        "comparison continuation must retain requestId, cursor, and token");

    Require(!client.RequestComparison("missing-token", "member-cursor-0008", 8),
        "a continuation cursor without a token must be rejected");
    Require(!client.RequestComparison("bad-token", "member-cursor-0008", 8,
        std::string(32, '!')), "an unsafe comparison token must be rejected");
}

void TestMalformedAckOkTypeBecomesInvalidResponse()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    std::vector<std::string> messages;
    client.SetMessageCallback([&](std::string message) {
        messages.push_back(std::move(message));
    });

    Require(client.UploadSnapshot(MakeSnapshot(909)),
        "malformed ACK fixture should enter the latest slot");
    Require(client.CompleteLatestSnapshotAckWithPayloadForTesting(
        R"({"ok":"yes","acceptedRevision":909})"),
        "malformed ACK should run through the production ACK handler");
    Require(client.DispatchMessages(8) == 1,
        "malformed ACK should emit one normalized result");
    Require(messages.back().find("\"code\":\"invalid_response\"") !=
        std::string::npos && HasRevision(messages.back(), 909),
        "non-boolean ACK ok must become invalid_response and retain revision");
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

void TestServerUrlTransportPolicy()
{
    struct UrlCase
    {
        const wchar_t* url;
        bool allowed;
    };
    const UrlCase cases[] = {
        { L"http://localhost:18880", true },
        { L"http://LOCALHOST:18880/base", true },
        { L"http://127.0.0.1:18880", true },
        { L"http://127.42.3.4:18880", true },
        { L"http://[::1]:18880", true },
        { L"http://8.8.8.8:18880", true },
        { L"http://10.0.0.5:18880", true },
        { L"http://192.168.1.5:18880", true },
        { L"http://example.com:18880", true },
        { L"http://127.example.com:18880", true },
        { L"https://8.8.8.8:18880", true },
        { L"https://10.0.0.5:18880", true },
        { L"https://example.com:18880/base", true },
        { L"http://user:password@localhost:18880", false },
        { L"https://user:password@example.com:18880", false },
        { L"http://localhost:18880/base?query=1", false },
        { L"https://example.com:18880/base#fragment", false },
    };

    for (const UrlCase& test : cases) {
        CloudMatchClient client;
        const bool configured = client.Configure(test.url,
            "url-policy-device", "url-policy-token");
        if (configured != test.allowed) {
            std::wcerr << L"URL policy mismatch: " << test.url << L" expected="
                << test.allowed << L" actual=" << configured << L'\n';
        }
        Require(configured == test.allowed,
            "server URL transport policy must allow explicit HTTP or HTTPS endpoints");
    }
}

void TestBroadcasterNameByteBoundaries()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    const std::string exactly512Bytes(512, 'a');
    const std::string over512Bytes(513, 'a');

    Require(client.JoinRoom("59", exactly512Bytes),
        "a 512-byte broadcaster name should reach server-side validation");
    Require(client.Rename(exactly512Bytes),
        "a 512-byte rename should reach server-side validation");
    Require(!client.JoinRoom("59", over512Bytes),
        "a broadcaster name over 512 bytes must be rejected locally");
    Require(!client.Rename(over512Bytes),
        "a rename over 512 bytes must be rejected locally");
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

void TestRenameOnlyCommitsRememberedIdentityAfterCurrentSuccessfulAck()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    client.SetDesiredJoinForReplayForTesting("59", "Confirmed Old Name");

    Require(client.Rename("Rejected Name"), "failed-ACK rename should queue");
    Require(client.RememberedBroadcasterNameForTesting() == "Confirmed Old Name",
        "queueing rename must not change remembered join identity");
    const std::uint64_t failedAck = client.SendNextRenameForTesting();
    Require(failedAck != 0 && client.CompleteRenameAckForTesting(failedAck, false,
        {}, "rate_limited"), "failed rename ACK should use the production handler");
    Require(client.RememberedBroadcasterNameForTesting() == "Confirmed Old Name",
        "failed rename ACK must preserve remembered join identity");

    Require(client.Rename("Timed Out Name"), "timeout rename should queue");
    const std::uint64_t timedOutAck = client.SendNextRenameForTesting();
    Require(timedOutAck != 0 && client.ExpireRenameAckForTesting(timedOutAck),
        "rename timeout should use the production expiry path");
    Require(client.RememberedBroadcasterNameForTesting() == "Confirmed Old Name",
        "rename timeout must preserve remembered join identity");

    Require(client.Rename("Disconnected Name"), "disconnect rename should queue");
    Require(client.SendNextRenameForTesting() != 0,
        "disconnect rename should enter pending ACK state");
    Require(client.FailPendingRenameAcksForTesting("connection_lost"),
        "connection loss should use the production pending-ACK failure path");
    Require(client.RememberedBroadcasterNameForTesting() == "Confirmed Old Name",
        "connection loss must preserve remembered join identity");

    Require(client.Rename("  Requested Name  "), "successful rename should queue");
    const std::uint64_t successfulAck = client.SendNextRenameForTesting();
    Require(successfulAck != 0 && client.CompleteRenameAckForTesting(successfulAck,
        true, "Requested Name", {}),
        "successful rename ACK should use the server-confirmed normalized name");
    Require(client.RememberedBroadcasterNameForTesting() == "Requested Name",
        "successful current-generation ACK must commit the confirmed name");
}

void TestRenameAckGenerationAndOrderingCannotRollbackRememberedIdentity()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    client.SetDesiredJoinForReplayForTesting("59", "Original Name");

    Require(client.Rename("First Name") && client.Rename("Second Name"),
        "rapid consecutive renames should queue on one connection");
    Require(client.RememberedBroadcasterNameForTesting() == "Original Name",
        "queued rapid renames must not mutate confirmed identity");
    const std::uint64_t firstAck = client.SendNextRenameForTesting();
    const std::uint64_t secondAck = client.SendNextRenameForTesting();
    Require(firstAck != 0 && secondAck != 0,
        "both rapid renames should enter pending ACK state");
    Require(client.CompleteRenameAckForTesting(secondAck, true, "Second Name", {}),
        "newer rename ACK should complete first");
    Require(client.RememberedBroadcasterNameForTesting() == "Second Name",
        "newer successful ACK should commit its confirmed name");
    Require(client.CompleteRenameAckForTesting(firstAck, true, "First Name", {}),
        "older rename ACK should still be consumed");
    Require(client.RememberedBroadcasterNameForTesting() == "Second Name",
        "late older ACK must not roll back the newer confirmed name");
    Require(client.GetStatusSnapshot().broadcasterName == "Second Name",
        "late older ACK must not roll back the confirmed status identity");

    Require(client.Rename("Stale Generation Name"),
        "stale-generation rename should queue before reconfigure");
    const std::uint64_t staleAck = client.SendNextRenameForTesting();
    Require(staleAck != 0, "stale-generation rename should enter pending ACK state");
    client.ConfigureForTesting();
    client.SetDesiredJoinForReplayForTesting("59", "Current Generation Name");
    Require(!client.CompleteRenameAckForTesting(staleAck, true,
        "Stale Generation Name", {}),
        "reconfigure must discard old-generation pending ACKs");
    Require(client.RememberedBroadcasterNameForTesting() == "Current Generation Name",
        "old-generation ACK must not change current remembered identity");
}

void TestSnapshotRequestsPreserveCorrelationAcrossFailuresAndGeneration()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    std::vector<std::string> messages;
    client.SetMessageCallback([&](std::string message) {
        messages.push_back(std::move(message));
    });

    Require(client.RequestSnapshot("snapshot-request-ok", "target-device-a", 41),
        "revision-bound snapshot request should be accepted");
    Require(client.CompleteNextProtectedOperationForTesting(),
        "snapshot request should complete through the protected result path");
    Require(client.DispatchMessages(4) == 1,
        "completed snapshot request should dispatch once");
    Require(messages.back().find("\"requestId\":\"snapshot-request-ok\"") !=
        std::string::npos &&
        messages.back().find("\"targetDeviceId\":\"target-device-a\"") !=
        std::string::npos &&
        messages.back().find("\"clientRevision\":41") != std::string::npos,
        "snapshot result must preserve request, device, and revision correlation");

    Require(client.RequestSnapshot("snapshot-request-timeout", "target-device-b", 52),
        "timeout snapshot request should be accepted");
    Require(client.FailNextProtectedOperationForTesting("timeout"),
        "timeout should use the pending request failure path");
    Require(client.DispatchMessages(4) == 1 &&
        messages.back().find("\"code\":\"timeout\"") != std::string::npos &&
        messages.back().find("\"requestId\":\"snapshot-request-timeout\"") !=
            std::string::npos &&
        messages.back().find("\"targetDeviceId\":\"target-device-b\"") !=
            std::string::npos &&
        messages.back().find("\"clientRevision\":52") != std::string::npos,
        "timeout result must preserve all snapshot request correlation fields");

    Require(client.RequestSnapshot("snapshot-request-disconnect", "target-device-c", 63),
        "disconnect snapshot request should be accepted");
    Require(client.FailNextProtectedOperationForTesting("connection_lost"),
        "disconnect should use the pending request failure path");
    Require(client.DispatchMessages(4) == 1 &&
        messages.back().find("\"code\":\"connection_lost\"") != std::string::npos &&
        messages.back().find("\"clientRevision\":63") != std::string::npos,
        "disconnect result must preserve the requested snapshot revision");

    Require(client.RequestSnapshot("stale-generation-request", "old-device", 70),
        "old generation request should enter the queue");
    client.ConfigureForTesting();
    Require(client.RequestSnapshot("current-generation-request", "new-device", 71),
        "current generation request should enter the queue");
    Require(client.CompleteNextProtectedOperationForTesting(),
        "current generation request should complete");
    Require(client.DispatchMessages(4) == 1 &&
        messages.back().find("current-generation-request") != std::string::npos &&
        messages.back().find("stale-generation-request") == std::string::npos,
        "reconfiguration must discard stale generation snapshot requests");
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

void TestTransientRequestsFailOfflineCancelAndExpireWithoutReplay()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    client.SetConnectedForTesting(false);
    for (std::size_t index = 0; index < 96; ++index) {
        Require(!client.RequestComparison("offline-comparison-" + std::to_string(index)),
            "offline comparison clicks must fail immediately");
        Require(!client.RequestSnapshot("offline-snapshot-" + std::to_string(index),
            "target-device-0001", 1),
            "offline snapshot clicks must fail immediately");
    }
    Require(client.PendingTransientRequestCountForTesting() == 0,
        "offline clicks must not consume command queue capacity");

    client.SetConnectedForTesting(true);
    Require(client.RequestComparison("cancel-comparison"),
        "connected comparison request should queue");
    Require(client.RequestSnapshot("cancel-snapshot", "target-device-0002", 2),
        "connected snapshot request should queue");
    Require(client.CancelRequest("cancel-comparison") &&
        client.CancelRequest("cancel-snapshot"),
        "panel lifecycle cancellation should remove requests by requestId");
    Require(client.PendingTransientRequestCountForTesting() == 0 &&
        !client.CompleteNextProtectedOperationForTesting(),
        "canceled requests must never send or produce delayed results");

    std::vector<std::string> messages;
    client.SetMessageCallback([&](std::string message) {
        messages.push_back(std::move(message));
    });
    Require(client.RequestComparison("expired-comparison"),
        "connected comparison request should queue before expiry");
    Require(client.ExpireNextTransientRequestForTesting(),
        "test clock should expire the unsent transient request");
    Require(client.DispatchMessages(4) == 1 &&
        messages.back().find("\"code\":\"timeout\"") != std::string::npos,
        "expired unsent request should report a recoverable timeout");
    client.SetConnectedForTesting(false);
    client.SetConnectedForTesting(true);
    Require(client.PendingTransientRequestCountForTesting() == 0 &&
        !client.CompleteNextProtectedOperationForTesting(),
        "expired requests must not replay after reconnect");
}

void TestPresenceRevisionDoesNotAdvanceComparisonRevision()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    Require(client.HandleServerEventForTesting("room:changed",
        R"({"roomId":"59","roomRevision":7,"comparisonRevision":7})"),
        "comparison change fixture should use the production event handler");
    CloudMatchStatusSnapshot status = client.GetStatusSnapshot();
    Require(status.roomRevision == 7 && status.comparisonRevision == 7,
        "room:changed must advance the comparison data revision");

    Require(client.HandleServerEventForTesting("room:presence",
        R"({"roomId":"59","roomRevision":99,"comparisonRevision":7,"presenceRevision":4,"deviceId":"peer-device","online":true})"),
        "presence fixture should use the production event handler");
    status = client.GetStatusSnapshot();
    Require(status.roomRevision == 7 && status.comparisonRevision == 7,
        "presence must not advance comparison data revision");
    Require(status.presenceRevision == 4,
        "presence events must advance only the presence revision");
}

void TestRateLimitedSnapshotAckPreservesRetryDelay()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    std::string message;
    client.SetMessageCallback([&](std::string value) {
        message = std::move(value);
    });

    Require(client.UploadSnapshot(MakeSnapshot(1001)),
        "rate-limited snapshot fixture should enter the latest slot");
    Require(client.CompleteLatestSnapshotAckWithPayloadForTesting(
        R"({"ok":false,"code":"rate_limited","retryAfterMs":1750})"),
        "rate-limited snapshot ACK should use the production ACK handler");
    Require(client.DispatchMessages(4) == 1,
        "rate-limited snapshot ACK should dispatch exactly once");
    Require(message.find("\"code\":\"rate_limited\"") != std::string::npos &&
        message.find("\"retryAfterMs\":1750") != std::string::npos &&
        HasRevision(message, 1001),
        "rate-limited upload result must retain retry delay and client revision");
}

void TestRealtimeSnapshotsCoalescePerBroadcaster()
{
    CloudMatchClient client;
    client.ConfigureForTesting();
    std::vector<std::string> messages;
    client.SetMessageCallback([&](std::string message) {
        messages.push_back(std::move(message));
    });

    Require(client.HandleServerEventForTesting("sync:realtime_snapshot",
        R"({"sourceDeviceId":"new-target-0002","snapshotRevision":2,"snapshot":{"clientRevision":2}})"),
        "new realtime target fixture should use the production event handler");
    Require(client.HandleServerEventForTesting("sync:realtime_snapshot",
        R"({"sourceDeviceId":"old-target-0001","snapshotRevision":1,"snapshot":{"clientRevision":1}})"),
        "late old-target fixture should use the production event handler");

    Require(client.DispatchMessages(8) == 2,
        "realtime snapshots from different broadcasters must not overwrite each other");
    Require(messages.size() == 2 &&
        messages[0].find("\"sourceDeviceId\":\"new-target-0002\"") != std::string::npos &&
        messages[1].find("\"sourceDeviceId\":\"old-target-0001\"") != std::string::npos,
        "realtime snapshots must retain source order for the UI-thread target filter");
}

} // namespace

int main()
{
    TestSnapshotResultsCarryLocalRevisionAndFilterOldGeneration();
    TestOversizedProtectedFallbackPreservesCorrelation();
    TestComparisonPaginationCarriesImmutableToken();
    TestMalformedAckOkTypeBecomesInvalidResponse();
    TestDispatchRunsCallbackOnCallerAndAllowsStop();
    TestConfigureDiscardsOldGenerationMessages();
    TestServerUrlTransportPolicy();
    TestBroadcasterNameByteBoundaries();
    TestProtectedResultCapacityBackpressuresAndRecovers();
    TestDesiredRoomCannotCrossConfigurationGeneration();
    TestRenameOnlyCommitsRememberedIdentityAfterCurrentSuccessfulAck();
    TestRenameAckGenerationAndOrderingCannotRollbackRememberedIdentity();
    TestSnapshotRequestsPreserveCorrelationAcrossFailuresAndGeneration();
    TestDispatchLifecycleGateWaitsAndRemainsReentrant();
    TestRememberedJoinRetriesAfterDispatchCapacityRelease();
    TestTransientRequestsFailOfflineCancelAndExpireWithoutReplay();
    TestPresenceRevisionDoesNotAdvanceComparisonRevision();
    TestRateLimitedSnapshotAckPreservesRetryDelay();
    TestRealtimeSnapshotsCoalescePerBroadcaster();
    std::cout << "Cloud match client tests passed.\n";
    return 0;
}
