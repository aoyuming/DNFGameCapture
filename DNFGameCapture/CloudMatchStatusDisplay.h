#pragma once

#include "CloudMatchClient.h"

#include <string>

enum class CloudMatchDisplayState
{
    online,
    working,
    offline,
    notJoined
};

struct CloudMatchDisplayContext
{
    bool hasJoinedRoom = false;
    bool hasPendingRoom = false;
    bool roomConfirmed = false;
    bool joining = false;
    bool registering = false;
    bool restoring = false;
    std::wstring roomName;
    std::wstring broadcasterName;
};

struct CloudMatchDisplayStatus
{
    CloudMatchDisplayState state = CloudMatchDisplayState::notJoined;
    std::wstring text;
};

CloudMatchDisplayStatus BuildCloudMatchDisplayStatus(
    const CloudMatchStatusSnapshot& clientStatus,
    const CloudMatchDisplayContext& context);
const char* CloudMatchDisplayStateName(CloudMatchDisplayState state);
