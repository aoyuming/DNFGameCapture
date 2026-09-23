#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "json.hpp"
#include "PlayerIdentityGroupService.h"

namespace dnf::player_library {
using EntityId = std::int64_t;
using IdentifierId = std::int64_t;
using RequestId = std::uint64_t;
using LegacyLibrary = std::map<std::wstring, std::vector<std::wstring>>;
inline constexpr int NormalizerVersion = 1;

enum class IdentifierKind { Game };
struct Identifier {
    IdentifierId identifierId = 0;
    IdentifierKind kind = IdentifierKind::Game;
    std::wstring canonicalKey;
    std::wstring displayText;
    std::vector<std::wstring> spellings;
};
struct PlayerEntity {
    EntityId entityId = 0;
    std::optional<std::string> cloudId;
    std::vector<std::wstring> names;
    // References into Snapshot::identifiers, not repeated raw ID strings.
    std::vector<IdentifierId> gameIds;
};
struct LookupResult {
    std::vector<EntityId> candidates;
    std::optional<IdentifierKind> matchedBy;
    bool conflict = false;
};
struct Snapshot {
    std::uint64_t revision = 0;
    bool persisted = false; // False only for initial, read-only migration fallback.
    std::vector<PlayerEntity> entities;
    std::vector<Identifier> identifiers;
    std::unordered_map<std::wstring, EntityId> nameIndex;
    std::unordered_map<std::wstring, std::vector<EntityId>> gameIndex;
    std::unordered_map<EntityId, std::size_t> entityIndex;
    std::unordered_map<IdentifierId, std::size_t> identifierIndex;
    LegacyLibrary legacy;
    std::map<std::wstring, std::wstring> formattedLegacy;
    nlohmann::json identityMetadata = nlohmann::json::object();
    // Overlap rows include commonGameIds from original evidence,
    // with exact displayed-list counts even when suggestion enumeration is capped.
    nlohmann::json identityState = nlohmann::json::object();
    nlohmann::json v2Entities = nlohmann::json::array();
    std::string legacyPayload;
    std::string appendPayload;
    const PlayerEntity* FindName(const std::wstring& name) const;
    const Identifier* FindIdentifier(IdentifierId id) const;
    LookupResult Lookup(const std::wstring& gameId) const;
};
using PlayerLibrarySnapshot = Snapshot;
using SnapshotPtr = std::shared_ptr<const Snapshot>;

std::wstring Trim(const std::wstring& text);
std::wstring CanonicalKey(const std::wstring& text, IdentifierKind kind);
std::string ToUtf8(const std::wstring& text);
std::wstring FromUtf8(const std::string& text);
LegacyLibrary ParseLegacy(const std::string& utf8);
std::string SerializeLegacy(const LegacyLibrary& library);
std::vector<std::wstring> ParseIds(const std::wstring& text);
std::wstring FormatIds(const std::vector<std::wstring>& ids);
std::string IdentityFingerprint(std::vector<std::wstring> names);
std::wstring ResolvePreferredLocalName(const Snapshot& snapshot,
    const std::wstring& incomingName,
    const std::vector<std::wstring>& incomingGameIds,
    const std::vector<std::wstring>& localFieldedNames);
// Compatibility boundary: discard retired fields without reinterpreting their values.
void PurgeRetiredIdentityFields(nlohmann::json& value);
// Worker/database only: rebuilds projections and reverse indexes from normalized objects.
void BuildSnapshotViews(Snapshot& snapshot);
// Uses saved constituent evidence, not merged unions, and honors durable exceptions.
// Snapshot indexes must already be built. Capped evidence is never promoted.
dnf::identity::IdentityAnalysis AnalyzeIdentityEvidence(const Snapshot& snapshot);
SnapshotPtr BuildFallbackSnapshot(const LegacyLibrary& library,
    const nlohmann::json& identityMetadata = nlohmann::json::object());
} // namespace dnf::player_library
