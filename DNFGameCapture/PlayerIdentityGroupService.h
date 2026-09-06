#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace dnf::identity {

inline constexpr std::size_t AUTO_GROUP_SHARED_ID_THRESHOLD = 4;
inline constexpr int AUTO_GROUP_POLICY_VERSION = 2;

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
};

struct IdentityAnalysis {
    std::vector<IdentityGroup> exactGroups;
    std::vector<OverlapSuggestion> overlapSuggestions;
};

using IdEquivalent = std::function<bool(const std::wstring&, const std::wstring&)>;

IdentityAnalysis Analyze(const std::vector<AliasEntry>& entries,
    const IdEquivalent& equivalent);

std::uint64_t ComputeAliasEntriesFingerprint(
    const std::vector<AliasEntry>& entries);

std::vector<std::wstring> UnionIds(
    const std::vector<std::vector<std::wstring>>& sources,
    const IdEquivalent& equivalent);

} // namespace dnf::identity
