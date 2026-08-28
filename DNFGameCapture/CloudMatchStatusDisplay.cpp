#include "CloudMatchStatusDisplay.h"

CloudMatchDisplayStatus BuildCloudMatchDisplayStatus(
    const CloudMatchStatusSnapshot& clientStatus,
    const CloudMatchDisplayContext& context)
{
    const bool hasRoom = context.hasJoinedRoom || context.hasPendingRoom;
    if (!hasRoom) {
        return { CloudMatchDisplayState::notJoined, L"\u672A\u52A0\u5165\u4E91\u7AEF\u623F\u95F4" };
    }

    const bool reconnecting = context.hasPendingRoom || context.joining ||
        context.registering || context.restoring || clientStatus.connecting ||
        clientStatus.reconnecting;
    if (reconnecting) {
        return { CloudMatchDisplayState::reconnecting,
            context.roomName + L" \u00B7 \u91CD\u8FDE\u4E2D" };
    }

    if (clientStatus.connected && context.roomConfirmed) {
        return { CloudMatchDisplayState::online,
            context.roomName + L" \u00B7 " + context.broadcasterName };
    }

    return { CloudMatchDisplayState::offline,
        context.roomName + L" \u00B7 \u79BB\u7EBF" };
}

const char* CloudMatchDisplayStateName(CloudMatchDisplayState state)
{
    switch (state) {
    case CloudMatchDisplayState::online:
        return "online";
    case CloudMatchDisplayState::reconnecting:
        return "reconnecting";
    case CloudMatchDisplayState::offline:
        return "offline";
    case CloudMatchDisplayState::notJoined:
    default:
        return "not-joined";
    }
}
