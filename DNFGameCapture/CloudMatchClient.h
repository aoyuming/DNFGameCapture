#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct CloudMatchStatusSnapshot
{
    bool configured = false;
    bool connecting = false;
    bool connected = false;
    bool reconnecting = false;
    std::string roomId;
    std::string roomName;
    std::string broadcasterName;
    std::string statusText;
    std::uint64_t roomRevision = 0;
};

class CloudMatchClient
{
public:
    using MessageCallback = std::function<void(std::string)>;

    CloudMatchClient();
    ~CloudMatchClient();

    CloudMatchClient(const CloudMatchClient&) = delete;
    CloudMatchClient& operator=(const CloudMatchClient&) = delete;

    bool Configure(const std::string& serverUrl, const std::string& deviceId,
        const std::string& deviceToken);
    bool Configure(const std::wstring& serverUrl, const std::string& deviceId,
        const std::string& deviceToken);
    bool Start();
    void Stop();

    bool RegisterDevice(const std::string& deviceId);
    bool JoinRoom(const std::string& roomId, const std::string& broadcasterName);
    bool Rename(const std::string& broadcasterName);
    bool LeaveRoom();
    bool UploadSnapshot(std::string snapshotJson);
    bool RequestComparison(const std::string& requestId);
    bool RequestComparison(unsigned int requestId);
    bool RequestSnapshot(const std::string& requestId, const std::string& targetDeviceId);
    bool RequestSnapshot(unsigned int requestId, const std::string& targetDeviceId);

    CloudMatchStatusSnapshot GetStatusSnapshot() const;
    void SetMessageCallback(MessageCallback callback);
    std::size_t DispatchMessages(std::size_t maxCount = 32);

#ifdef CLOUD_MATCH_CLIENT_STANDALONE
    std::uint64_t ConfigureForTesting();
    bool CompleteNextProtectedOperationForTesting();
    bool JoinRoomForGenerationForTesting(std::uint64_t generation,
        const std::string& roomId, const std::string& broadcasterName);
    bool HasDesiredRoomForTesting() const;
    std::uint64_t DesiredRoomGenerationForTesting() const;
    void SetDesiredJoinForReplayForTesting(const std::string& roomId,
        const std::string& broadcasterName);
    std::string RetryRememberedJoinForTesting();
    std::size_t RememberedJoinSendCountForTesting() const;
#endif

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
