#pragma once

#include "json.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

struct DnfCloudMatchPreviewBinding
{
    std::string roomId;
    std::uint64_t connectionGeneration = 0;
    std::uint64_t roomRevision = 0;
    std::string requestId;
    std::string deviceId;
    std::uint64_t snapshotRevision = 0;
};

struct DnfCloudMatchCurrentState : DnfCloudMatchPreviewBinding
{
    bool connected = false;
    bool roomConfirmed = false;
};

struct DnfCloudMatchUndoGuard
{
    bool available = false;
    std::uint64_t postApplyEpoch = 0;
    std::string postApplyHash;
    std::string roomId;
    std::uint64_t connectionGeneration = 0;
};

using DnfCloudMatchNameNormalizer =
    std::function<bool(const std::string&, std::string&)>;

bool DnfNormalizeCloudMatchUtf8Name(const std::string& input,
    std::string& normalized);

bool DnfConvertCloudMatchSnapshot(const nlohmann::json& cloudSnapshot,
    std::uint64_t expectedClientRevision, bool swapped,
    const DnfCloudMatchNameNormalizer& normalizeName,
    nlohmann::json& teamSnapshot, std::string& errorCode);

// Identity-only additive import; stable across scores and seat order changes.
bool DnfBuildSyncedPlayerLibrary(const nlohmann::json& teamSnapshot,
    nlohmann::json& libraryPayload);

// Keep accepted evidence in order; repeats may coalesce, distinct batches may not.
class DnfSyncedPlayerLibraryQueue
{
public:
    bool Enqueue(const nlohmann::json& payload, bool force);
    bool IsOutstanding(const nlohmann::json& payload) const;
    nlohmann::json Begin();
    void Complete(bool success);
    void InvalidateCommitted() { committed_ = nullptr; }
    bool HasPending() const noexcept { return !pending_.empty(); }
private:
    std::deque<nlohmann::json> pending_;
    nlohmann::json active_;
    nlohmann::json committed_;
};

nlohmann::json DnfBuildCloudMatchPreview(
    const nlohmann::json& localTeamSnapshot,
    const nlohmann::json& remoteTeamSnapshot, bool swapped);

bool DnfResolveCloudMatchRelativeSwap(const nlohmann::json& members,
    const std::string& localDeviceId, const std::string& targetDeviceId,
    bool& relativeSwap, std::string& errorCode);

bool DnfIsCloudMatchPreviewCurrent(const DnfCloudMatchPreviewBinding& binding,
    const DnfCloudMatchCurrentState& current) noexcept;

std::string DnfCloudMatchContentHash(const nlohmann::json& snapshot);

bool DnfCanCloudMatchUndo(const DnfCloudMatchUndoGuard& guard,
    std::uint64_t currentEpoch, const std::string& currentHash,
    const std::string& currentRoomId,
    std::uint64_t currentConnectionGeneration) noexcept;
