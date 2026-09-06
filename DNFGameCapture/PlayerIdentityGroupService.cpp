#include "PlayerIdentityGroupService.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>

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

bool SameIdSet(const std::vector<std::wstring>& left,
    const std::vector<std::wstring>& right, const IdEquivalent& equivalent)
{
    const auto leftUnique = UniqueIds(left, equivalent);
    const auto rightUnique = UniqueIds(right, equivalent);
    if (leftUnique.size() != rightUnique.size()) return false;
    return std::all_of(leftUnique.begin(), leftUnique.end(), [&](const auto& id) {
        return std::any_of(rightUnique.begin(), rightUnique.end(),
            [&](const auto& other) { return equivalent(id, other); });
    });
}

std::size_t CommonIdCount(const std::vector<std::wstring>& left,
    const std::vector<std::wstring>& right, const IdEquivalent& equivalent)
{
    const auto leftUnique = UniqueIds(left, equivalent);
    const auto rightUnique = UniqueIds(right, equivalent);
    std::size_t count = 0;
    for (const auto& id : leftUnique) {
        if (std::any_of(rightUnique.begin(), rightUnique.end(),
            [&](const auto& other) { return equivalent(id, other); })) {
            ++count;
        }
    }
    return count;
}

bool ShouldAutoGroup(const AliasEntry& left, const AliasEntry& right,
    const IdEquivalent& equivalent)
{
    return SameIdSet(left.ids, right.ids, equivalent) ||
        CommonIdCount(left.ids, right.ids, equivalent) >=
            AUTO_GROUP_SHARED_ID_THRESHOLD;
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

} // namespace

IdentityAnalysis Analyze(const std::vector<AliasEntry>& entries,
    const IdEquivalent& equivalent)
{
    IdentityAnalysis result;
    if (!equivalent) return result;

    std::vector<bool> grouped(entries.size(), false);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (grouped[i] || UniqueIds(entries[i].ids, equivalent).empty()) continue;

        IdentityGroup group;
        std::vector<std::size_t> groupIndexes = { i };
        group.names.push_back(entries[i].name);
        grouped[i] = true;
        for (std::size_t cursor = 0; cursor < groupIndexes.size(); ++cursor) {
            const std::size_t memberIndex = groupIndexes[cursor];
            for (std::size_t j = 0; j < entries.size(); ++j) {
                if (grouped[j] || UniqueIds(entries[j].ids, equivalent).empty()) continue;
                if (!ShouldAutoGroup(entries[memberIndex], entries[j], equivalent)) continue;
                group.names.push_back(entries[j].name);
                groupIndexes.push_back(j);
                grouped[j] = true;
            }
        }
        if (group.names.size() >= 2) {
            group.groupId = StableGroupId(group.names);
            result.exactGroups.push_back(std::move(group));
        }
        else {
            grouped[i] = false;
        }
    }

    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (UniqueIds(entries[i].ids, equivalent).empty()) continue;
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            if (UniqueIds(entries[j].ids, equivalent).empty()) continue;
            const std::size_t common = CommonIdCount(entries[i].ids,
                entries[j].ids, equivalent);
            if (common == 0 || common >= AUTO_GROUP_SHARED_ID_THRESHOLD ||
                SameIdSet(entries[i].ids, entries[j].ids, equivalent)) {
                continue;
            }
            result.overlapSuggestions.push_back({ entries[i].name, entries[j].name, common });
        }
    }

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
