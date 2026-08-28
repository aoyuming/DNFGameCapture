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
    std::uint64_t generation = 0;
    std::uint64_t connectionGeneration = 0;
    std::uint64_t roomRevision = 0;
    std::uint64_t comparisonRevision = 0;
    std::uint64_t presenceRevision = 0;
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
    bool JoinUnifiedPool(const std::string& broadcasterName);
    bool Rename(const std::string& broadcasterName);
    bool RenameUnified(const std::string& broadcasterName);
    bool LeaveRoom();
    bool UploadSnapshot(std::string snapshotJson);
    bool RequestBroadcasterDirectory(const std::string& requestId);
    bool RequestBroadcasterSnapshot(const std::string& requestId,
        const std::string& targetDeviceId);
    bool RequestSyncHistory(const std::string& requestId);
    bool RequestSyncRelations(const std::string& requestId);
    bool RecordCloudSync(const std::string& requestId,
        const std::string& targetDeviceId, const std::string& targetName,
        const std::string& syncType, std::uint64_t snapshotRevision,
        bool merged);
    bool StartRealtimeSync(const std::string& requestId,
        const std::string& targetDeviceId, const std::string& targetName);
    bool HeartbeatRealtimeSync(const std::string& requestId);
    bool StopRealtimeSync(const std::string& requestId);
    bool RequestComparison(const std::string& requestId,
        const std::string& cursor = {}, std::uint32_t limit = 64,
        const std::string& comparisonToken = {});
    bool RequestComparison(unsigned int requestId);
    bool RequestSnapshot(const std::string& requestId,
        const std::string& targetDeviceId, std::uint64_t clientRevision);
    bool RequestSnapshot(unsigned int requestId,
        const std::string& targetDeviceId, std::uint64_t clientRevision);
    bool CancelRequest(const std::string& requestId);

    CloudMatchStatusSnapshot GetStatusSnapshot() const;
    void SetMessageCallback(MessageCallback callback);
    std::size_t DispatchMessages(std::size_t maxCount = 32);

#ifdef CLOUD_MATCH_CLIENT_STANDALONE
    std::uint64_t ConfigureForTesting();
    bool CompleteNextProtectedOperationForTesting(std::size_t responsePadding = 0);
    bool FailNextProtectedOperationForTesting(const std::string& code);
    bool CompleteLatestSnapshotAckForTesting(bool ok,
        std::uint64_t acceptedRevision, const std::string& code,
        std::size_t responsePadding = 0);
    bool CompleteLatestSnapshotAckWithPayloadForTesting(
        const std::string& payloadJson);
    bool ExpireLatestSnapshotAckForTesting();
    bool FailLatestSnapshotAckForTesting(const std::string& code);
    bool JoinRoomForGenerationForTesting(std::uint64_t generation,
        const std::string& roomId, const std::string& broadcasterName);
    bool HasDesiredRoomForTesting() const;
    std::uint64_t DesiredRoomGenerationForTesting() const;
    void SetDesiredJoinForReplayForTesting(const std::string& roomId,
        const std::string& broadcasterName);
    std::string RetryRememberedJoinForTesting();
    std::size_t RememberedJoinSendCountForTesting() const;
    std::string RememberedBroadcasterNameForTesting() const;
    std::uint64_t SendNextRenameForTesting();
    bool CompleteRenameAckForTesting(std::uint64_t ackId, bool ok,
        const std::string& confirmedBroadcasterName, const std::string& code);
    bool ExpireRenameAckForTesting(std::uint64_t ackId);
    bool FailPendingRenameAcksForTesting(const std::string& code);
    void SetConnectedForTesting(bool connected);
    std::size_t PendingTransientRequestCountForTesting() const;
    bool ExpireNextTransientRequestForTesting();
    bool HandleServerEventForTesting(const std::string& eventName,
        const std::string& payloadJson);
#endif

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
