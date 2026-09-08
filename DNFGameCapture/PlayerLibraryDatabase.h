#pragma once

#include "PlayerLibraryModel.h"
#include <filesystem>

namespace dnf::player_library {
struct Options {
    std::filesystem::path databasePath; // Empty: %APPDATA%/DNFGameCapture/player_library.db.
    std::filesystem::path legacyPath; // Empty: executable directory/alias_db.ini.
    std::filesystem::path identityGroupsPath; // Empty: legacy directory/player_identity_groups.json.
    bool legacyExportEnabled = true;
    // Explicit, previously authoritative SQLite source. Used only if databasePath
    // is absent; copied via SQLite backup, never moved or modified. An existing
    // destination always wins. A missing source permits legacy migration; an
    // unreadable/corrupt source fails without exporting or falling back to INI.
    std::filesystem::path priorDatabasePath;
};
Options ResolveOptions(Options options);

struct ImportConflict {
    std::string entityId;
    std::vector<std::wstring> names;
    std::string reason;
};
struct ImportReport {
    std::size_t acceptedEntities = 0;
    std::vector<ImportConflict> skipped;
};

// Single-thread owned. Methods throw std::exception on failure, never publish uncommitted data.
class PlayerLibraryDatabase {
public:
    explicit PlayerLibraryDatabase(Options options = {});
    ~PlayerLibraryDatabase();
    PlayerLibraryDatabase(const PlayerLibraryDatabase&) = delete;
    PlayerLibraryDatabase& operator=(const PlayerLibraryDatabase&) = delete;
    SnapshotPtr Initialize();
    SnapshotPtr LoadSnapshot();
    SnapshotPtr ReplaceLegacy(const LegacyLibrary& library,
        const nlohmann::json& identityMetadata = nullptr);
    // entities array, {entities}, {data:{entities}}, or legacy {players}/{data:{players}}.
    // Import is additive; absent names/IDs never delete local data.
    // Automatically persists conservative unions at >=5 distinct shared game IDs.
    // No opt-in is needed for no-entityId match-sync batches.
    // Original ID sets and cloud provenance remain in split metadata for restoration
    // and reimport. Compact autoSplitNameSets supplement legacy fingerprint exceptions.
    // Versioned responses require outer importSource (endpoint). Per-source revision
    // watermarks commit atomically; stale responses fail and normalized no-ops keep revision.
    // Public pulls may opt into skipOwnershipConflicts. Report is published only
    // after commit; malformed data, stale revisions and storage errors still fail.
    SnapshotPtr ImportV2(const nlohmann::json& payload, ImportReport* report = nullptr);
    // action (also cmd_identity_*): create_player, merge, add_alias, rename_name, rename_id, update_ids,
    // unmerge, delete_alias, ignore_overlap, ignore_overlaps, refresh. Mutation requires uint64 revision.
    // ignore_overlaps: non-empty pairs:[{leftName,rightName}], all must be current
    // suggestions. Deduplicates unordered pairs and persists only supplied pairs atomically.
    // names/name/alias/groupId, sourceName/newName, ids,
    // leftName/rightName and splitAll use the existing frontend field names.
    // create_player: name (trimmed, validated, must not exist), ids (required string
    // array, may be empty). Interns/deduplicates game IDs and atomically creates one
    // local entity; name-only players remain in v2 export.
    // Invalid input, stale revision or persistence failure leaves the database unchanged.
    // rename_name: name/newName. rename_id: name/oldId/newId, applied to the whole group.
    // unmerge requires keepNewIdsWith (a member name) when post-merge IDs exist.
    // reset_local requires confirmed:true and the current revision. Backs up SQLite
    // and compatibility files (including players_config.txt), clears local data/migration audits/cloud watermarks,
    // and advances the local revision once. Recovery folder: localResetBackup metadata.
    // Does not modify cloud data; failed resets roll back the database and restore files.
    SnapshotPtr ExecuteIdentity(const nlohmann::json& command);
    void ExportLegacy(const Snapshot& snapshot);
    SnapshotPtr LoadLegacyFallback() const;
    const Options& GetOptions() const noexcept;
    const std::string& GetWarning() const noexcept;
private:
    SnapshotPtr ResetLocal(const nlohmann::json& command);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dnf::player_library
