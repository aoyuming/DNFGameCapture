#pragma once

#include "PlayerLibraryModel.h"
#include <algorithm>
#include <array>
#include <functional>
#include <unordered_set>

namespace dnf::identity {

struct OcrActivePlayer {
    std::wstring name;
    int team = -1;
    std::vector<std::wstring> gameIds;

    bool operator==(const OcrActivePlayer& other) const {
        return name == other.name && team == other.team && gameIds == other.gameIds;
    }
};

struct OcrGameIdMetadata {
    std::wstring fullMatchName;
    std::wstring matchName;
    std::wstring declaredArea;
    std::wstring declaredJob;
    bool isSymbolicId = false;
};

struct OcrGameCandidate {
    std::size_t playerIndex = 0;
    std::wstring name;
    OcrGameIdMetadata metadata;
};

// Owned by one dialog and serialized by its identity mutex. This type has no
// scoreboard fields, alias-statistics indices, or borrowed snapshot pointers.
class OcrLookupCache {
public:
    using Roster = std::array<OcrActivePlayer, 8>;
    using ParseGameId = std::function<OcrGameIdMetadata(const std::wstring&)>;

    bool Prepare(player_library::SnapshotPtr snapshot, const Roster& roster,
        const ParseGameId& parse)
    {
        using namespace player_library;
        const auto revision = snapshot ? snapshot->revision : 0;
        if (prepared_ && snapshot_ == snapshot && revision_ == revision && roster_ == roster) return false;

        std::vector<OcrGameCandidate> games;
        std::unordered_map<std::wstring, OcrGameIdMetadata> parsedIds;
        std::unordered_map<std::wstring, std::wstring> rawKeys;
        for (std::size_t slot = 0; slot < roster.size(); ++slot) {
            const auto& player = roster[slot];
            if (player.name.empty() || (player.team != 0 && player.team != 1)) continue;
            const auto* entity = snapshot ? snapshot->FindName(player.name) : nullptr;
            std::unordered_map<std::wstring, std::wstring> normalizedIds;
            if (entity) {
                for (const auto id : entity->gameIds) {
                    const auto* value = snapshot->FindIdentifier(id);
                    if (value && value->kind == IdentifierKind::Game)
                        normalizedIds.emplace(value->canonicalKey, value->displayText);
                }
            }

            // Active aliases remain authoritative for game-ID statistics, including
            // unsaved edits. Snapshot identifiers supply their normalized metadata.
            std::unordered_set<std::wstring> seen;
            for (const auto& raw : player.gameIds) {
                auto cachedKey = rawKeys.find(raw);
                if (cachedKey == rawKeys.end()) cachedKey = rawKeys.emplace(raw,
                    CanonicalKey(raw, IdentifierKind::Game)).first;
                const auto& key = cachedKey->second;
                if (key.empty() || !seen.insert(key).second) continue;
                auto metadata = parsedIds.find(key);
                if (metadata == parsedIds.end()) {
                    const auto normalized = normalizedIds.find(key);
                    metadata = parsedIds.emplace(key,
                        parse(normalized == normalizedIds.end() ? raw : normalized->second)).first;
                }
                games.push_back({slot, raw, metadata->second});
            }
        }
        snapshot_ = std::move(snapshot);
        revision_ = revision;
        roster_ = roster;
        gameCandidates_ = std::move(games);
        prepared_ = true;
        return true;
    }
    const std::vector<OcrGameCandidate>& GameCandidates() const { return gameCandidates_; }

    void Reset() {
        snapshot_.reset();
        revision_ = 0;
        roster_ = {};
        prepared_ = false;
        gameCandidates_.clear();
    }

private:
    player_library::SnapshotPtr snapshot_;
    std::uint64_t revision_ = 0;
    Roster roster_{};
    bool prepared_ = false;
    std::vector<OcrGameCandidate> gameCandidates_;
};

} // namespace dnf::identity
