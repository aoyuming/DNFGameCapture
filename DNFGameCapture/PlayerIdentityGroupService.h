#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dnf::identity {

inline constexpr std::size_t AUTO_GROUP_SHARED_ID_THRESHOLD = 5;
inline constexpr int AUTO_GROUP_POLICY_VERSION = 5;

struct AliasEntry {
    std::wstring name;
    std::vector<std::wstring> ids;
};

struct IdentityGroup {
    std::wstring groupId;
    std::vector<std::wstring> names;
};

struct OverlapSuggestion {
    std::wstring leftName;
    std::wstring rightName;
    std::size_t commonIdCount = 0;
    // True when capped index buckets may contribute additional shared IDs.
    bool commonIdCountIsLowerBound = false;
};

struct WeakIdEvidence {
    std::wstring canonicalKey;
    std::vector<std::wstring> names;
};

struct IdentityAnalysis {
    std::vector<IdentityGroup> exactGroups;
    std::vector<OverlapSuggestion> overlapSuggestions;
    // Compact display evidence for capped suggestions, not a separate merge signal.
    std::vector<WeakIdEvidence> weakIdEvidence;
    // True if any pair counting or suggestion expansion was capped; inspect weakIdEvidence.
    bool overlapSuggestionsTruncated = false;
};

using IdEquivalent = std::function<bool(const std::wstring&, const std::wstring&)>;
using IdCanonicalKey = std::function<std::wstring(const std::wstring&)>;

IdentityAnalysis Analyze(const std::vector<AliasEntry>& entries,
    const IdEquivalent& equivalent);

// The callback must be deterministic: equal keys mean equivalent IDs. Empty keys
// are ignored. An absent callback uses the original, case-sensitive ID verbatim.
// Names, groups and suggestions are ordered independently of input order.
// Canonicalization is cached once per distinct non-empty raw game ID in this call.
// Exact sets bypass pair work only if they satisfy the game-ID threshold.
// Automatic grouping streams complete original-set clique checks using linear
// auxiliary memory; dense input can require quadratic time. No evidence is dropped.
// Only suggestion display expands buckets of at most 256 distinct sets, with
// 1,000,000 pair updates and 100,000 rows. Capped suggestion counts are lower bounds.
IdentityAnalysis AnalyzeIndexed(const std::vector<AliasEntry>& entries,
    const IdCanonicalKey& canonicalKey = {});

std::uint64_t ComputeAliasEntriesFingerprint(
    const std::vector<AliasEntry>& entries);

std::vector<std::wstring> UnionIds(
    const std::vector<std::vector<std::wstring>>& sources,
    const IdEquivalent& equivalent);

} // namespace dnf::identity
