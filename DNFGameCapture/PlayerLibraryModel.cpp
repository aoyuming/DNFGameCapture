#include "PlayerLibraryModel.h"
#include "PlayerIdentityGroupService.h"
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace dnf::player_library {
namespace {
nlohmann::json Strings(const std::vector<std::wstring>& values) {
    auto result = nlohmann::json::array();
    for (const auto& value : values) result.push_back(ToUtf8(value));
    return result;
}
std::vector<std::wstring> ReadNames(const nlohmann::json& value) {
    std::vector<std::wstring> result;
    if (value.is_array()) for (const auto& item : value)
        if (item.is_string()) result.push_back(FromUtf8(item.get<std::string>()));
    return result;
}
}
std::wstring Trim(const std::wstring& text) {
    auto begin = std::find_if_not(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c) != 0; });
    auto end = std::find_if_not(text.rbegin(), text.rend(), [](wchar_t c) { return std::iswspace(c) != 0; }).base();
    return begin < end ? std::wstring(begin, end) : L"";
}
std::wstring CanonicalKey(const std::wstring& text, IdentifierKind) {
    auto raw = Trim(text);
    if (raw.empty()) return raw;
    const auto sharp = raw.find_first_of(L"#\xff03");
    const auto body = Trim(raw.substr(0, sharp));
    // Preserve exact case and code points, matching DnfAliasSameStorageEntry.
    // NFC/case folding would collapse entries the legacy comparator distinguishes.
    if (body.empty()) return raw;
    const auto job = sharp == std::wstring::npos ? L"" : Trim(raw.substr(sharp + 1));
    return body + L"\x1f" + job;
}
std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) throw std::invalid_argument("Text too long");
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::invalid_argument("Invalid UTF-16");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring FromUtf8(const std::string& text) {
    if (text.empty()) return {};
    if (text.size() > INT_MAX) throw std::invalid_argument("Text too long");
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw std::invalid_argument("Invalid UTF-8");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
std::vector<std::wstring> ParseIds(const std::wstring& text) {
    std::vector<std::wstring> result;
    const std::wstring delimiters = L"()\xff08\xff09 \t\r\n";
    for (auto pos = text.find_first_not_of(delimiters); pos != std::wstring::npos;) {
        const auto end = text.find_first_of(delimiters, pos);
        result.push_back(text.substr(pos, end == std::wstring::npos ? end : end - pos));
        pos = end == std::wstring::npos ? end : text.find_first_not_of(delimiters, end);
    }
    return result;
}
std::wstring FormatIds(const std::vector<std::wstring>& ids) {
    std::wstring result;
    std::set<std::wstring> seen;
    for (const auto& id : ids) if (!Trim(id).empty() && seen.insert(CanonicalKey(id, IdentifierKind::Game)).second)
        result += L"(" + Trim(id) + L")";
    return result;
}
LegacyLibrary ParseLegacy(const std::string& utf8) {
    auto content = FromUtf8(utf8);
    if (!content.empty() && content.front() == 0xfeff) content.erase(content.begin());
    LegacyLibrary library;
    std::wistringstream stream(content);
    std::wstring line;
    while (std::getline(stream, line)) {
        line.erase(std::remove(line.begin(), line.end(), L'\r'), line.end());
        line = Trim(line);
        if (line.empty()) continue;
        const auto eq = line.find(L'=');
        if (eq == std::wstring::npos) {
            if (line.front() == L';' || line.front() == L'[') continue;
            throw std::invalid_argument("Malformed legacy library line");
        }
        auto name = Trim(line.substr(0, eq));
        if (name.empty()) throw std::invalid_argument("Empty legacy name");
        auto ids = ParseIds(line.substr(eq + 1));
        auto& values = library[name];
        // Retain duplicate-line data and spelling variants for lossless migration.
        for (auto& id : ids) if (std::find(values.begin(), values.end(), id) == values.end()) values.push_back(std::move(id));
    }
    return library;
}
std::string SerializeLegacy(const LegacyLibrary& library) {
    std::wstring result;
    for (const auto& [name, ids] : library) result += name + L"=" + FormatIds(ids) + L"\r\n";
    return ToUtf8(result);
}
std::string IdentityFingerprint(std::vector<std::wstring> names) {
    std::sort(names.begin(), names.end());
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& name : names) {
        for (unsigned char c : ToUtf8(name)) { hash ^= c; hash *= 1099511628211ULL; }
        hash ^= 0x1f; hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}
const PlayerEntity* Snapshot::FindName(const std::wstring& name) const {
    const auto it = nameIndex.find(Trim(name));
    if (it == nameIndex.end()) return nullptr;
    const auto entity = entityIndex.find(it->second);
    return entity == entityIndex.end() ? nullptr : &entities[entity->second];
}
const Identifier* Snapshot::FindIdentifier(IdentifierId id) const {
    const auto it = identifierIndex.find(id);
    return it == identifierIndex.end() ? nullptr : &identifiers[it->second];
}
LookupResult Snapshot::Lookup(const std::wstring& gameId) const {
    LookupResult result;
    auto game = gameIndex.find(CanonicalKey(gameId, IdentifierKind::Game));
    if (game != gameIndex.end()) { result.candidates = game->second; result.matchedBy = IdentifierKind::Game; }
    result.conflict = result.candidates.size() > 1;
    return result;
}
namespace {
std::vector<dnf::identity::AliasEntry> CollectIdentityEvidence(const Snapshot& s) {
    std::unordered_map<std::wstring, const nlohmann::json*> managed;
    if (s.identityMetadata.contains("groups")) for (const auto& group : s.identityMetadata["groups"])
        if (group.contains("beforeMerge") || group.value("autoGroupNeedsReview", false))
            for (const auto& name : ReadNames(group["names"])) managed[name] = &group;
    std::vector<dnf::identity::AliasEntry> entries;
    for (const auto& entity : s.entities) {
        std::set<std::wstring> currentGame;
        for (auto ref : entity.gameIds) currentGame.insert(s.FindIdentifier(ref)->canonicalKey);
        for (const auto& name : entity.names) {
            dnf::identity::AliasEntry entry; entry.name = name;
            const auto saved = managed.find(name);
            if (saved != managed.end()) {
                const auto& group = *saved->second;
                const auto key = ToUtf8(name);
                if (group.value("autoGroupNeedsReview", false)) continue;
                if (group.contains("beforeMerge") && group["beforeMerge"].contains(key)) entry.ids = ReadNames(group["beforeMerge"][key]);
                // Keep split snapshots intact, but deleted relations are no longer
                // matching evidence. Never substitute the entity's merged union.
                entry.ids.erase(std::remove_if(entry.ids.begin(), entry.ids.end(), [&](const auto& id) {
                    return currentGame.count(CanonicalKey(id, IdentifierKind::Game)) == 0;
                }), entry.ids.end());
            } else {
                for (auto ref : entity.gameIds) entry.ids.push_back(s.FindIdentifier(ref)->displayText);
            }
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}
dnf::identity::IdentityAnalysis AnalyzeEvidence(const Snapshot& s,
    const std::vector<dnf::identity::AliasEntry>& entries) {
    auto analysis = dnf::identity::AnalyzeIndexed(entries,
        [](const auto& id) { return CanonicalKey(id, IdentifierKind::Game); });
    std::set<std::string> ignored;
    for (const auto& v : s.identityMetadata.value("autoSplitFingerprints", nlohmann::json::array()))
        if (v.is_string()) ignored.insert(v.get<std::string>());
    // One indexed name set represents all pair exceptions from a split, avoiding
    // quadratic fingerprint storage and preserving exceptions for later subsets.
    std::unordered_map<std::wstring, std::vector<std::size_t>> splitMembership;
    if (s.identityMetadata.contains("autoSplitNameSets")) {
        std::size_t index = 0;
        for (const auto& names : s.identityMetadata["autoSplitNameSets"]) {
            for (const auto& name : ReadNames(names)) splitMembership[name].push_back(index);
            ++index;
        }
    }
    const auto splitPair = [&](const std::wstring& left, const std::wstring& right) {
        const auto a = splitMembership.find(left), b = splitMembership.find(right);
        if (a == splitMembership.end() || b == splitMembership.end()) return false;
        return std::any_of(a->second.begin(), a->second.end(), [&](auto index) { return std::binary_search(b->second.begin(), b->second.end(), index); });
    };
    analysis.exactGroups.erase(std::remove_if(analysis.exactGroups.begin(), analysis.exactGroups.end(), [&](const auto& group) {
        std::unordered_map<std::size_t, std::size_t> splitCounts;
        for (const auto& name : group.names) {
            const auto found = splitMembership.find(name);
            if (found != splitMembership.end()) for (auto index : found->second) if (++splitCounts[index] >= 2) return true;
        }
        if (ignored.empty()) return false;
        if (ignored.count(IdentityFingerprint(group.names))) return true;
        const auto size = group.names.size();
        // Stream legacy pair exceptions without dropping unrelated large groups.
        for (std::size_t i = 0; i < size; ++i) for (std::size_t j = i + 1; j < size; ++j)
            if (ignored.count(IdentityFingerprint({group.names[i], group.names[j]}))) return true;
        return false;
    }), analysis.exactGroups.end());
    analysis.overlapSuggestions.erase(std::remove_if(analysis.overlapSuggestions.begin(), analysis.overlapSuggestions.end(), [&](const auto& pair) {
        return s.FindName(pair.leftName)->entityId == s.FindName(pair.rightName)->entityId ||
            splitPair(pair.leftName, pair.rightName) ||
            ignored.count(IdentityFingerprint({pair.leftName, pair.rightName})) != 0;
    }), analysis.overlapSuggestions.end());
    return analysis;
}
using EvidenceDisplays = std::map<std::wstring, std::wstring>;
EvidenceDisplays IndexEvidenceDisplays(const std::vector<std::wstring>& ids, IdentifierKind kind) {
    EvidenceDisplays result;
    for (const auto& id : ids) {
        auto key = CanonicalKey(id, kind);
        if (!key.empty()) result.emplace(std::move(key), Trim(id));
    }
    return result;
}
std::vector<std::wstring> CommonEvidenceDisplays(const EvidenceDisplays& left, const EvidenceDisplays& right) {
    std::vector<std::wstring> result;
    auto a = left.begin(), b = right.begin();
    while (a != left.end() && b != right.end()) {
        if (a->first < b->first) ++a;
        else if (b->first < a->first) ++b;
        else { result.push_back(a->second); ++a; ++b; }
    }
    return result;
}
}
dnf::identity::IdentityAnalysis AnalyzeIdentityEvidence(const Snapshot& s) {
    return AnalyzeEvidence(s, CollectIdentityEvidence(s));
}
void BuildSnapshotViews(Snapshot& s) {
    PurgeRetiredIdentityFields(s.identityMetadata);
    s.nameIndex.clear(); s.gameIndex.clear(); s.entityIndex.clear(); s.identifierIndex.clear();
    s.legacy.clear(); s.formattedLegacy.clear(); s.v2Entities = nlohmann::json::array();
    for (std::size_t i = 0; i < s.identifiers.size(); ++i) s.identifierIndex.emplace(s.identifiers[i].identifierId, i);
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        const auto& entity = s.entities[i]; s.entityIndex.emplace(entity.entityId, i);
        std::vector<std::wstring> game;
        for (auto id : entity.gameIds) {
            const auto* value = s.FindIdentifier(id);
            if (!value || value->kind != IdentifierKind::Game) throw std::runtime_error("Invalid game link");
            game.push_back(value->displayText); s.gameIndex[value->canonicalKey].push_back(entity.entityId);
        }
        for (const auto& name : entity.names) {
            if (!s.nameIndex.emplace(name, entity.entityId).second) throw std::runtime_error("Duplicate player name");
            s.legacy.emplace(name, game); s.formattedLegacy.emplace(name, FormatIds(game));
        }
        nlohmann::json item = {{"names", Strings(entity.names)}, {"gameIds", Strings(game)}};
        if (entity.cloudId) item["entityId"] = *entity.cloudId;
        s.v2Entities.push_back(std::move(item));
    }
    for (auto& [key, ids] : s.gameIndex) {
        std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
    nlohmann::json payload = nlohmann::json::object(), append = payload;
    s.identityState = {{"revision", s.revision}, {"groups", nlohmann::json::array()}, {"entries", nlohmann::json::array()},
        {"exactMatches", nlohmann::json::array()}, {"overlapSuggestions", nlohmann::json::array()}};
    for (const auto& [name, ids] : s.legacy) {
        payload[ToUtf8(name)] = ToUtf8(s.formattedLegacy.at(name));
        if (!ids.empty()) append[ToUtf8(name)] = payload[ToUtf8(name)];
        s.identityState["entries"].push_back({{"name", ToUtf8(name)}, {"ids", Strings(ids)}});
    }
    s.legacyPayload = payload.dump(); s.appendPayload = append.dump();
    std::set<std::wstring> claimed;
    auto appendGroup = [&](const std::string& id, const std::string& source, const std::vector<std::wstring>& names) {
        if (names.size() < 2) return;
        std::vector<std::wstring> ids; std::set<std::wstring> seen;
        std::set<EntityId> visited;
        for (const auto& name : names) {
            claimed.insert(name);
            const auto* owner = s.FindName(name);
            if (owner && !visited.insert(owner->entityId).second) continue;
            for (const auto& value : s.legacy.at(name)) if (seen.insert(CanonicalKey(value, IdentifierKind::Game)).second) ids.push_back(value);
        }
        s.identityState["groups"].push_back({{"groupId", id}, {"source", source}, {"names", Strings(names)}, {"ids", Strings(ids)}});
    };
    if (s.identityMetadata.contains("groups")) for (const auto& group : s.identityMetadata["groups"]) {
        auto names = ReadNames(group.value("names", nlohmann::json::array()));
        names.erase(std::remove_if(names.begin(), names.end(), [&](const auto& n) { return !s.nameIndex.count(n); }), names.end());
        std::set<std::wstring> seenNames(names.begin(), names.end());
        std::set<EntityId> visited;
        if (group.value("source", "manual") != "automatic" || group.contains("beforeEntities")) for (std::size_t i = 0; i < names.size(); ++i) {
            const auto* entity = s.FindName(names[i]);
            if (entity && visited.insert(entity->entityId).second)
                for (const auto& name : entity->names) if (seenNames.insert(name).second) names.push_back(name);
        }
        appendGroup(group.value("groupId", ""), group.value("source", "manual"), names);
    }
    for (const auto& e : s.entities) if (e.names.size() >= 2 && std::none_of(e.names.begin(), e.names.end(), [&](const auto& name) { return claimed.count(name) != 0; }))
        appendGroup("entity-" + std::to_string(e.entityId), "manual", e.names);
    const auto evidence = CollectIdentityEvidence(s);
    const auto analysis = AnalyzeEvidence(s, evidence);
    s.identityState["overlapSuggestionsTruncated"] = analysis.overlapSuggestionsTruncated;
    for (const auto& group : analysis.exactGroups) {
        if (std::any_of(group.names.begin(), group.names.end(), [&](const auto& n) { return claimed.count(n) != 0; })) continue;
        appendGroup(ToUtf8(group.groupId), "automatic", group.names);
        s.identityState["exactMatches"].push_back({{"groupId", ToUtf8(group.groupId)}, {"names", Strings(group.names)}});
    }
    std::unordered_map<std::wstring, EvidenceDisplays> displays;
    if (!analysis.overlapSuggestions.empty()) for (const auto& entry : evidence)
        displays.emplace(entry.name, IndexEvidenceDisplays(entry.ids, IdentifierKind::Game));
    for (const auto& overlap : analysis.overlapSuggestions) {
        const auto& left = displays.at(overlap.leftName);
        const auto& right = displays.at(overlap.rightName);
        const auto games = CommonEvidenceDisplays(left, right);
        // The suggestion set may be capped, but these original-set intersections
        // are complete. Display exact counts without changing analysis/merge rules.
        s.identityState["overlapSuggestions"].push_back({{"leftName", ToUtf8(overlap.leftName)}, {"rightName", ToUtf8(overlap.rightName)},
            {"commonGameIds", Strings(games)},
            {"commonIdCount", games.size()}, {"commonIdCountIsLowerBound", false}});
    }
}
void PurgeRetiredIdentityFields(nlohmann::json& value) {
    if (value.is_array()) {
        for (auto& item : value) PurgeRetiredIdentityFields(item);
    } else if (value.is_object()) {
        for (const auto* key : {"adventureGroupIds", "adventureGroupId", "adventureIds", "adventureId",
            "beforeAdventure", "commonAdventureIds", "commonAdventureIdCount", "adventureIndex"}) value.erase(key);
        for (auto& [key, item] : value.items()) {
            // These objects are keyed by player names, which must never be purged.
            if (key == "beforeMerge") continue;
            if (key == "beforeEntities" && item.is_object()) {
                for (auto& [name, original] : item.items()) PurgeRetiredIdentityFields(original);
            } else PurgeRetiredIdentityFields(item);
        }
    }
}
SnapshotPtr BuildFallbackSnapshot(const LegacyLibrary& library, const nlohmann::json& metadata) {
    auto s = std::make_shared<Snapshot>(); s->revision = 1; s->identityMetadata = metadata;
    std::map<std::wstring, IdentifierId> pool;
    for (const auto& [name, ids] : library) {
        PlayerEntity e; e.entityId = static_cast<EntityId>(s->entities.size() + 1); e.names.push_back(name);
        for (const auto& raw : ids) {
            const auto key = CanonicalKey(raw, IdentifierKind::Game);
            if (key.empty()) continue;
            auto [it, inserted] = pool.emplace(key, static_cast<IdentifierId>(s->identifiers.size() + 1));
            if (inserted) s->identifiers.push_back({it->second, IdentifierKind::Game, key, Trim(raw), {}});
            if (std::find(e.gameIds.begin(), e.gameIds.end(), it->second) == e.gameIds.end()) e.gameIds.push_back(it->second);
        }
        s->entities.push_back(std::move(e));
    }
    try { BuildSnapshotViews(*s); }
    catch (const std::exception&) {
        if (s->identityMetadata.empty()) throw;
        s->identityMetadata = nlohmann::json::object(); BuildSnapshotViews(*s);
    }
    return s;
}
} // namespace dnf::player_library
