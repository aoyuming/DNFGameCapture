#include "CloudMatchStatusDisplay.h"

CloudMatchDisplayStatus BuildCloudMatchDisplayStatus(
    const CloudMatchStatusSnapshot& clientStatus,
    const CloudMatchDisplayContext& context)
{
    const bool hasRoom = context.hasJoinedRoom || context.hasPendingRoom;
    if (!hasRoom) {
        return { CloudMatchDisplayState::notJoined, L"未加入云端房间" };
    }

    const bool working = context.hasPendingRoom || context.joining ||
        context.registering || context.restoring || clientStatus.connecting ||
        clientStatus.reconnecting;
    if (working) {
        return { CloudMatchDisplayState::working,
            context.roomName + L" · 重连中" };
    }

    if (clientStatus.connected && context.roomConfirmed) {
        return { CloudMatchDisplayState::online,
            context.roomName + L" · " + context.broadcasterName };
    }

    return { CloudMatchDisplayState::offline,
        context.roomName + L" · 离线" };
}

const char* CloudMatchDisplayStateName(CloudMatchDisplayState state)
{
    switch (state) {
    case CloudMatchDisplayState::online:
        return "online";
    case CloudMatchDisplayState::working:
        return "working";
    case CloudMatchDisplayState::offline:
        return "offline";
    case CloudMatchDisplayState::notJoined:
    default:
        return "not-joined";
    }
}
