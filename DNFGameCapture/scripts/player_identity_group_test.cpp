#include "../PlayerIdentityGroupService.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using dnf::identity::AliasEntry;
using dnf::identity::IdentityAnalysis;

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool SameId(const std::wstring& left, const std::wstring& right)
{
    return left == right;
}

void TestExactGroupsIgnoreEmptyEntries()
{
    const std::vector<AliasEntry> entries = {
        {L"王大枪", {L"A", L"B"}},
        {L"老王", {L"B", L"A"}},
        {L"空名称", {}},
        {L"另一个空名称", {}}
    };

    const IdentityAnalysis analysis = dnf::identity::Analyze(entries, SameId);
    Require(analysis.exactGroups.size() == 1,
        "only non-empty identical ID sets should be grouped");
    Require(analysis.exactGroups[0].names.size() == 2,
        "the exact group should contain both matching names");
}

void TestPartialOverlapIsOnlySuggestion()
{
    const std::vector<AliasEntry> entries = {
        {L"王大枪", {L"A", L"B"}},
        {L"旋律", {L"B", L"C"}}
    };

    const IdentityAnalysis analysis = dnf::identity::Analyze(entries, SameId);
    Require(analysis.exactGroups.empty(),
        "partially overlapping ID sets must not auto-merge");
    Require(analysis.overlapSuggestions.size() == 1,
        "partial overlap should produce one merge suggestion");
    Require(analysis.overlapSuggestions[0].commonIdCount == 1,
        "the suggestion should report the shared ID count");
}

void TestStrongOverlapAutoGroups()
{
    const std::vector<AliasEntry> entries = {
        {L"白羽", {L"A", L"B", L"C", L"D", L"白羽专属"}},
        {L"老白", {L"A", L"B", L"C", L"D", L"老白专属"}}
    };

    const IdentityAnalysis analysis = dnf::identity::Analyze(entries, SameId);
    Require(analysis.exactGroups.size() == 1,
        "four or more shared IDs should create an automatic identity group");
    Require(analysis.exactGroups[0].names.size() == 2,
        "the strong-overlap group should contain both names");
    Require(analysis.overlapSuggestions.empty(),
        "strong-overlap names should not remain in manual suggestions");
}

void TestCurrentAutoGroupPolicyVersion()
{
    Require(dnf::identity::AUTO_GROUP_POLICY_VERSION == 2,
        "the current auto-group policy must be version 2");
}

void TestThreeSharedIdsRemainSuggestion()
{
    const std::vector<AliasEntry> entries = {
        {L"选手甲", {L"A", L"B", L"C", L"甲专属"}},
        {L"选手乙", {L"A", L"B", L"C", L"乙专属"}}
    };

    const IdentityAnalysis analysis = dnf::identity::Analyze(entries, SameId);
    Require(analysis.exactGroups.empty(),
        "three shared IDs must remain below the automatic grouping threshold");
    Require(analysis.overlapSuggestions.size() == 1 &&
        analysis.overlapSuggestions[0].commonIdCount == 3,
        "three shared IDs should remain a manual suggestion");
}

void TestUnionDeduplicatesIDs()
{
    const std::vector<std::vector<std::wstring>> sources = {
        {L"A", L"B"},
        {L"B", L"C"},
        {L"A", L"D"}
    };

    const std::vector<std::wstring> merged =
        dnf::identity::UnionIds(sources, SameId);
    Require(merged == std::vector<std::wstring>({L"A", L"B", L"C", L"D"}),
        "manual merge should produce an ordered deduplicated union");
}

void TestAliasEntryFingerprintChangesWhenIDsChange()
{
    const std::vector<AliasEntry> original = {
        {L"白羽", {L"A", L"B"}},
        {L"老白", {L"A", L"B"}}
    };
    const std::vector<AliasEntry> changed = {
        {L"白羽", {L"A", L"B", L"C"}},
        {L"老白", {L"A", L"B"}}
    };

    Require(dnf::identity::ComputeAliasEntriesFingerprint(original) ==
        dnf::identity::ComputeAliasEntriesFingerprint(original),
        "the same alias database must produce a stable fingerprint");
    Require(dnf::identity::ComputeAliasEntriesFingerprint(original) !=
        dnf::identity::ComputeAliasEntriesFingerprint(changed),
        "changing an alias ID must invalidate the identity state cache");
}

} // namespace

int main()
{
    TestExactGroupsIgnoreEmptyEntries();
    TestPartialOverlapIsOnlySuggestion();
    TestStrongOverlapAutoGroups();
    TestCurrentAutoGroupPolicyVersion();
    TestThreeSharedIdsRemainSuggestion();
    TestUnionDeduplicatesIDs();
    TestAliasEntryFingerprintChangesWhenIDsChange();
    std::cout << "Player identity group tests passed.\n";
    return 0;
}
