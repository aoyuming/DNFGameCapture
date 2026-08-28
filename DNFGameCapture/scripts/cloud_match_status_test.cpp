#include "CloudMatchStatusDisplay.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void Require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

void RequireStatus(const CloudMatchDisplayStatus& actual,
    CloudMatchDisplayState expectedState, const wchar_t* expectedText,
    const char* message)
{
    Require(actual.state == expectedState, message);
    Require(actual.text == expectedText, message);
}
}

int main()
{
    CloudMatchStatusSnapshot client;
    client.configured = true;
    client.connected = true;

    CloudMatchDisplayContext context;
    RequireStatus(BuildCloudMatchDisplayStatus(client, context),
        CloudMatchDisplayState::notJoined, L"未加入云端房间",
        "a configured client without a room must remain not joined");

    context.hasJoinedRoom = true;
    context.roomConfirmed = true;
    context.roomName = L"李永房";
    context.broadcasterName = L"主播甲";
    RequireStatus(BuildCloudMatchDisplayStatus(client, context),
        CloudMatchDisplayState::online, L"李永房 · 主播甲",
        "a confirmed connected room must show its actual broadcaster");

    client.connected = false;
    client.connecting = true;
    RequireStatus(BuildCloudMatchDisplayStatus(client, context),
        CloudMatchDisplayState::working, L"李永房 · 重连中",
        "an initial connection must use the shared reconnecting label");

    client.connecting = false;
    client.reconnecting = true;
    RequireStatus(BuildCloudMatchDisplayStatus(client, context),
        CloudMatchDisplayState::working, L"李永房 · 重连中",
        "a reconnect must use the shared reconnecting label");

    client.reconnecting = false;
    RequireStatus(BuildCloudMatchDisplayStatus(client, context),
        CloudMatchDisplayState::offline, L"李永房 · 离线",
        "a saved room without a connection must show offline");

    context = {};
    context.hasPendingRoom = true;
    context.joining = true;
    context.roomName = L"温柔房";
    context.broadcasterName = L"主播乙";
    RequireStatus(BuildCloudMatchDisplayStatus(client, context),
        CloudMatchDisplayState::working, L"温柔房 · 重连中",
        "an in-progress room join must expose the target room");

    Require(std::string(CloudMatchDisplayStateName(
        CloudMatchDisplayState::online)) == "online",
        "online state name must be stable for Web JSON");
    Require(std::string(CloudMatchDisplayStateName(
        CloudMatchDisplayState::working)) == "working",
        "working state name must be stable for Web JSON");
    Require(std::string(CloudMatchDisplayStateName(
        CloudMatchDisplayState::offline)) == "offline",
        "offline state name must be stable for Web JSON");
    Require(std::string(CloudMatchDisplayStateName(
        CloudMatchDisplayState::notJoined)) == "not-joined",
        "not-joined state name must be stable for Web JSON");

    std::cout << "Cloud match status display tests passed.\n";
    return 0;
}
