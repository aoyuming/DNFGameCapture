#include "PlayerIdentityGroupService.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace dnf::identity {
namespace {

std::vector<std::wstring> UniqueIds(const std::vector<std::wstring>& ids,
    const IdEquivalent& equivalent)
{
    std::vector<std::wstring> result;
    for (const auto& id : ids) {
        if (id.empty()) continue;
        const bool duplicate = std::any_of(result.begin(), result.end(),
            [&](const auto& existing) { return equivalent(existing, id); });
        if (!duplicate) result.push_back(id);
    }
    return result;
}

std::size_t CommonIdCount(const std::vector<std::wstring>& left,
    const std::vector<std::wstring>& right, const IdEquivalent& equivalent)
{
    std::size_t count = 0;
    for (const auto& id : left) {
        if (std::any_of(right.begin(), right.end(),
            [&](const auto& other) { return equivalent(id, other); })) {
            ++count;
        }
    }
    return count;
}

std::wstring StableGroupId(std::vector<std::wstring> names)
{
    std::sort(names.begin(), names.end());
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& name : names) {
        for (const wchar_t ch : name) {
            hash ^= static_cast<std::uint64_t>(ch);
            hash *= 1099511628211ull;
        }
        hash ^= 0xffull;
        hash *= 1099511628211ull;
    }

    std::wostringstream out;
    out << L"identity-" << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    return out.str();
}

// These budgets limit suggestion display only, never automatic identity evidence.
constexpr std::size_t MAX_INDEX_BUCKET_SETS = 256;
constexpr std::size_t MAX_INDEX_PAIR_UPDATES = 1000000;
constexpr std::size_t MAX_INDEX_SUGGESTIONS = 100000;

struct AliasCluster {
    std::vector<std::size_t> ids;
    std::vector<std::wstring> names;
};

struct PairOverlap {
    std::size_t left;
    std::size_t right;
    std::size_t common;
    bool lowerBound = false;

    bool IsStrong() const {
        return common >= AUTO_GROUP_SHARED_ID_THRESHOLD;
    }
};

struct IdSetHash {
    std::size_t operator()(const std::vector<std::size_t>& ids) const {
        std::size_t hash = ids.size();
        for (const auto id : ids) {
            hash ^= id + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

using IndexPair = std::pair<std::size_t, std::size_t>;
struct IndexPairHash {
    std::size_t operator()(const IndexPair& pair) const {
        return pair.first ^ (pair.second + 0x9e3779b9u +
            (pair.first << 6) + (pair.first >> 2));
    }
};

std::vector<std::size_t> EntryOrder(const std::vector<AliasEntry>& entries)
{
    std::vector<std::size_t> order(entries.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (entries[a].name != entries[b].name) return entries[a].name < entries[b].name;
        return entries[a].ids < entries[b].ids;
    });
    return order;
}

bool HasStrongOverlap(const AliasCluster& left, const AliasCluster& right)
{
    std::size_t i = 0, j = 0, games = 0;
    while (i < left.ids.size() && j < right.ids.size()) {
        if (left.ids[i] < right.ids[j]) ++i;
        else if (right.ids[j] < left.ids[i]) ++j;
        else {
            if (++games >= AUTO_GROUP_SHARED_ID_THRESHOLD) return true;
            ++i; ++j;
        }
    }
    return false;
}

std::vector<std::size_t> CompleteIndexedGroups(const std::vector<AliasCluster>& clusters,
    const std::vector<std::vector<std::size_t>>& postings)
{
    const auto unassigned = clusters.size();
    std::vector<std::size_t> groupOf(clusters.size(), unassigned);
    std::vector<std::size_t> counts(clusters.size());
    for (std::size_t seed = 0; seed < clusters.size(); ++seed) {
        if (groupOf[seed] != unassigned) continue;
        groupOf[seed] = seed;
        std::size_t possibleGame = 0;
        for (auto id : clusters[seed].ids) if (postings[id].size() > 1)
            ++possibleGame;
        if (possibleGame < AUTO_GROUP_SHARED_ID_THRESHOLD) continue;

        // Count one seed at a time without dropping popular buckets or retaining
        // a quadratic graph. Exact duplicate signatures were already compressed.
        std::vector<std::size_t> candidates;
        for (auto id : clusters[seed].ids) for (auto candidate : postings[id]) {
            if (candidate <= seed || groupOf[candidate] != unassigned) continue;
            auto& count = counts[candidate];
            if (count == 0) candidates.push_back(candidate);
            ++count;
        }
        std::sort(candidates.begin(), candidates.end());
        std::vector<std::size_t> members{seed};
        for (auto candidate : candidates) {
            const auto count = counts[candidate]; counts[candidate] = {};
            if (count < AUTO_GROUP_SHARED_ID_THRESHOLD) continue;
            // Every original set must qualify, not the growing union of members.
            if (!std::all_of(members.begin() + 1, members.end(), [&](auto member) {
                return HasStrongOverlap(clusters[member], clusters[candidate]);
            })) continue;
            members.push_back(candidate); groupOf[candidate] = seed;
        }
    }
    return groupOf;
}

IdentityAnalysis BuildAnalysis(const std::vector<AliasCluster>& clusters,
    std::vector<PairOverlap>& overlaps, std::size_t suggestionLimit,
    std::vector<std::size_t> groupOf = {})
{
    IdentityAnalysis result;
    std::sort(overlaps.begin(), overlaps.end(), [](const auto& a, const auto& b) {
        return IndexPair(a.left, a.right) < IndexPair(b.left, b.right);
    });
    if (groupOf.empty()) {
        std::vector<std::vector<std::size_t>> neighbors(clusters.size());
        for (const auto& pair : overlaps) {
            if (!pair.IsStrong()) continue;
            neighbors[pair.left].push_back(pair.right);
            neighbors[pair.right].push_back(pair.left);
        }
        for (auto& adjacent : neighbors) std::sort(adjacent.begin(), adjacent.end());
        const auto unassigned = clusters.size();
        groupOf.assign(clusters.size(), unassigned);
        for (std::size_t seed = 0; seed < clusters.size(); ++seed) {
            if (groupOf[seed] != unassigned) continue;
            std::vector<std::size_t> members{seed};
            groupOf[seed] = seed;
            // Deterministic clique cover: every added set must match every member.
            for (const auto candidate : neighbors[seed]) {
                if (groupOf[candidate] != unassigned) continue;
                const auto& adjacent = neighbors[candidate];
                if (!std::all_of(members.begin(), members.end(), [&](std::size_t member) {
                    return std::binary_search(adjacent.begin(), adjacent.end(), member);
                })) continue;
                members.push_back(candidate);
                groupOf[candidate] = seed;
            }
        }
    }
    std::vector<std::vector<std::wstring>> namesByGroup(clusters.size());
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        auto& names = namesByGroup[groupOf[i]];
        names.insert(names.end(), clusters[i].names.begin(), clusters[i].names.end());
    }
    for (auto& names : namesByGroup) {
        if (names.size() < 2) continue;
        std::sort(names.begin(), names.end());
        auto id = StableGroupId(names);
        result.exactGroups.push_back({std::move(id), std::move(names)});
    }
    std::sort(result.exactGroups.begin(), result.exactGroups.end(),
        [](const auto& a, const auto& b) { return a.names < b.names; });

    for (const auto& pair : overlaps) {
        if (groupOf[pair.left] == groupOf[pair.right]) continue;
        // Include strong edges rejected by the clique check, not just weak edges.
        for (const auto& left : clusters[pair.left].names) {
            for (const auto& right : clusters[pair.right].names) {
                if (result.overlapSuggestions.size() == suggestionLimit) {
                    result.overlapSuggestionsTruncated = true;
                    break;
                }
                result.overlapSuggestions.push_back({(std::min)(left, right),
                    (std::max)(left, right), pair.common, pair.lowerBound});
            }
            if (result.overlapSuggestionsTruncated) break;
        }
        if (result.overlapSuggestionsTruncated) break;
    }
    std::sort(result.overlapSuggestions.begin(), result.overlapSuggestions.end(),
        [](const auto& a, const auto& b) {
            if (a.leftName != b.leftName) return a.leftName < b.leftName;
            if (a.rightName != b.rightName) return a.rightName < b.rightName;
            return a.commonIdCount < b.commonIdCount;
        });
    return result;
}

} // namespace

IdentityAnalysis Analyze(const std::vector<AliasEntry>& entries,
    const IdEquivalent& equivalent)
{
    if (!equivalent) return {};
    std::vector<AliasCluster> clusters;
    std::vector<std::vector<std::wstring>> uniqueIds;
    for (const auto index : EntryOrder(entries)) {
        auto ids = UniqueIds(entries[index].ids, equivalent);
        if (ids.empty()) continue;
        uniqueIds.push_back(std::move(ids));
        clusters.push_back({{}, {entries[index].name}});
    }
    std::vector<PairOverlap> overlaps;
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        for (std::size_t j = i + 1; j < clusters.size(); ++j) {
            const auto common = CommonIdCount(uniqueIds[i], uniqueIds[j], equivalent);
            if (common == 0) continue;
            overlaps.push_back({i, j, common});
        }
    }
    return BuildAnalysis(clusters, overlaps, (std::numeric_limits<std::size_t>::max)());
}

IdentityAnalysis AnalyzeIndexed(const std::vector<AliasEntry>& entries,
    const IdCanonicalKey& canonicalKey)
{
    const auto invalidId = (std::numeric_limits<std::size_t>::max)();
    std::unordered_map<std::wstring, std::size_t> rawIds;
    std::unordered_map<std::wstring, std::size_t> internedKeys;
    std::vector<std::wstring> keys;
    std::unordered_map<std::vector<std::size_t>, std::size_t, IdSetHash> signatures;
    std::vector<AliasCluster> clusters;
    for (const auto index : EntryOrder(entries)) {
        const auto& entry = entries[index];
        std::vector<std::size_t> ids;
        ids.reserve(entry.ids.size());
        for (const auto& raw : entry.ids) {
            if (raw.empty()) continue;
            auto cached = rawIds.find(raw);
            if (cached == rawIds.end()) {
                auto key = canonicalKey ? canonicalKey(raw) : raw;
                std::size_t id = invalidId;
                if (!key.empty()) {
                    const auto inserted = internedKeys.emplace(key, keys.size());
                    id = inserted.first->second;
                    if (inserted.second) keys.push_back(std::move(key));
                }
                cached = rawIds.emplace(raw, id).first;
            }
            if (cached->second != invalidId) ids.push_back(cached->second);
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        if (ids.empty()) continue;
        if (ids.size() < AUTO_GROUP_SHARED_ID_THRESHOLD) {
            clusters.push_back({std::move(ids), {entry.name}});
            continue;
        }
        const auto inserted = signatures.emplace(ids, clusters.size());
        if (inserted.second) clusters.push_back({std::move(ids), {}});
        clusters[inserted.first->second].names.push_back(entry.name);
    }

    std::vector<std::vector<std::size_t>> postings(keys.size());
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        for (const auto id : clusters[i].ids) postings[id].push_back(i);
    }
    std::vector<std::size_t> bucketOrder(keys.size());
    std::iota(bucketOrder.begin(), bucketOrder.end(), 0);
    std::sort(bucketOrder.begin(), bucketOrder.end(), [&](std::size_t a, std::size_t b) {
        if (postings[a].size() != postings[b].size()) return postings[a].size() < postings[b].size();
        return keys[a] < keys[b];
    });

    std::vector<bool> omittedKeys(keys.size(), false);
    std::vector<bool> incompleteSets(clusters.size(), false);
    std::unordered_map<IndexPair, std::size_t, IndexPairHash> counts;
    std::size_t remainingUpdates = MAX_INDEX_PAIR_UPDATES;
    bool bucketsCapped = false;
    for (const auto id : bucketOrder) {
        const auto& bucket = postings[id];
        if (bucket.size() < 2) continue;
        const bool popular = bucket.size() > MAX_INDEX_BUCKET_SETS;
        const auto updates = popular ? 0 : bucket.size() * (bucket.size() - 1) / 2;
        if (popular || updates > remainingUpdates) {
            omittedKeys[id] = true;
            bucketsCapped = true;
            for (const auto member : bucket) incompleteSets[member] = true;
            continue;
        }
        remainingUpdates -= updates;
        for (std::size_t i = 0; i < bucket.size(); ++i) {
            for (std::size_t j = i + 1; j < bucket.size(); ++j) {
                auto& count = counts[{bucket[i], bucket[j]}];
                ++count;
            }
        }
    }
    std::vector<PairOverlap> overlaps;
    overlaps.reserve(counts.size());
    for (const auto& counted : counts) {
        const auto left = counted.first.first;
        const auto right = counted.first.second;
        overlaps.push_back({left, right, counted.second,
            incompleteSets[left] && incompleteSets[right]});
    }
    auto result = BuildAnalysis(clusters, overlaps, MAX_INDEX_SUGGESTIONS,
        CompleteIndexedGroups(clusters, postings));
    const bool outputCapped = result.overlapSuggestionsTruncated;
    result.overlapSuggestionsTruncated = outputCapped || bucketsCapped;
    for (std::size_t id = 0; id < keys.size(); ++id) {
        if (!omittedKeys[id] && !(outputCapped && postings[id].size() > 1)) continue;
        WeakIdEvidence evidence{keys[id], {}};
        for (const auto member : postings[id]) {
            const auto& names = clusters[member].names;
            evidence.names.insert(evidence.names.end(), names.begin(), names.end());
        }
        std::sort(evidence.names.begin(), evidence.names.end());
        result.weakIdEvidence.push_back(std::move(evidence));
    }
    std::sort(result.weakIdEvidence.begin(), result.weakIdEvidence.end(),
        [](const auto& a, const auto& b) {
            return a.canonicalKey < b.canonicalKey;
        });
    return result;
}

std::uint64_t ComputeAliasEntriesFingerprint(
    const std::vector<AliasEntry>& entries)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& entry : entries) {
        for (const wchar_t ch : entry.name) {
            hash ^= static_cast<std::uint64_t>(ch);
            hash *= 1099511628211ull;
        }
        hash ^= 0x1full;
        hash *= 1099511628211ull;
        for (const auto& id : entry.ids) {
            for (const wchar_t ch : id) {
                hash ^= static_cast<std::uint64_t>(ch);
                hash *= 1099511628211ull;
            }
            hash ^= 0x1eull;
            hash *= 1099511628211ull;
        }
        hash ^= 0x1dull; hash *= 1099511628211ull;
        hash ^= 0xffull;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<std::wstring> UnionIds(
    const std::vector<std::vector<std::wstring>>& sources,
    const IdEquivalent& equivalent)
{
    std::vector<std::wstring> result;
    if (!equivalent) return result;
    for (const auto& source : sources) {
        for (const auto& id : source) {
            if (id.empty()) continue;
            const bool duplicate = std::any_of(result.begin(), result.end(),
                [&](const auto& existing) { return equivalent(existing, id); });
            if (!duplicate) result.push_back(id);
        }
    }
    return result;
}

} // namespace dnf::identity
