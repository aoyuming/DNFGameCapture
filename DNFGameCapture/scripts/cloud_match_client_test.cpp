#include "../CloudMatchClient.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
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

} // namespace

int main()
{
    TestDispatchRunsCallbackOnCallerAndAllowsStop();
    TestConfigureDiscardsOldGenerationMessages();
    std::cout << "Cloud match client tests passed.\n";
    return 0;
}
