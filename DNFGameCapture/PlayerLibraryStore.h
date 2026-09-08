#pragma once

#include "PlayerLibraryDatabase.h"

namespace dnf::player_library {
struct Result {
    RequestId requestId = 0;
    bool ok = false;
    std::string error;
    std::string message;
    SnapshotPtr snapshot; // Last persisted snapshot on failure; read-only fallback on initial failure.
    nlohmann::json command = nullptr; // Echoed only after a successful ExecuteIdentity.
    ImportReport importReport;
};

class PlayerLibraryStore {
public:
    explicit PlayerLibraryStore(Options options = {});
    ~PlayerLibraryStore();
    PlayerLibraryStore(const PlayerLibraryStore&) = delete;
    PlayerLibraryStore& operator=(const PlayerLibraryStore&) = delete;
    RequestId Initialize();
    RequestId SubmitLegacy(LegacyLibrary library, nlohmann::json identityMetadata = nullptr);
    RequestId ImportV2(nlohmann::json payload);
    RequestId ExecuteIdentity(nlohmann::json command);
    RequestId RequestRefresh();
    SnapshotPtr GetSnapshot() const noexcept;
    std::vector<Result> Drain();
    // Nonblocking: rejects new requests, cancels queued work, lets the in-flight transaction finish.
    // Worker owns shared state, never a Store/dialog pointer. Drain remains valid after Shutdown.
    void Shutdown() noexcept;
    bool IsStopped() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
    RequestId Enqueue(int kind, LegacyLibrary library, nlohmann::json payload);
};
} // namespace dnf::player_library
