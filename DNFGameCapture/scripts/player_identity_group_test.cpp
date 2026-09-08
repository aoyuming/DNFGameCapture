#include "../PlayerIdentityGroupService.h"

#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
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
        {L"王大枪", {L"A", L"B", L"C", L"D", L"E"}},
        {L"老王", {L"B", L"A", L"C", L"D", L"E"}},
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
        {L"白羽", {L"A", L"B", L"C", L"D", L"E", L"白羽专属"}},
        {L"老白", {L"A", L"B", L"C", L"D", L"E", L"老白专属"}}
    };

    const IdentityAnalysis analysis = dnf::identity::Analyze(entries, SameId);
    Require(analysis.exactGroups.size() == 1,
        "five or more shared IDs should create an automatic identity group");
    Require(analysis.exactGroups[0].names.size() == 2,
        "the strong-overlap group should contain both names");
    Require(analysis.overlapSuggestions.empty(),
        "strong-overlap names should not remain in manual suggestions");
}

void TestCurrentAutoGroupPolicyVersion()
{
    Require(dnf::identity::AUTO_GROUP_POLICY_VERSION == 5,
        "the current auto-group policy must be version 5");
}

void TestChainDoesNotAutoMerge()
{
    const std::vector<AliasEntry> entries = {
        {L"A", {L"1", L"2", L"3", L"4", L"ab"}},
        {L"B", {L"1", L"2", L"3", L"4", L"ab", L"5", L"6", L"7", L"8", L"bc"}},
        {L"C", {L"5", L"6", L"7", L"8", L"bc"}}
    };
    const auto analysis = dnf::identity::Analyze(entries, SameId);
    Require(analysis.exactGroups.size() == 1 &&
        analysis.exactGroups[0].names == std::vector<std::wstring>({L"A", L"B"}),
        "A-B and B-C strong links must not automatically merge incompatible A-C");
    Require(analysis.overlapSuggestions.size() == 1 &&
        analysis.overlapSuggestions[0].leftName == L"B" &&
        analysis.overlapSuggestions[0].rightName == L"C" &&
        analysis.overlapSuggestions[0].commonIdCount == 5,
        "a rejected strong chain link must remain a manual suggestion");

    const auto indexed = dnf::identity::AnalyzeIndexed(entries);
    Require(indexed.exactGroups.size() == 1 &&
        indexed.exactGroups[0].names == analysis.exactGroups[0].names,
        "indexed grouping must also reject incompatible chain merges");
    Require(indexed.overlapSuggestions.size() == 1 &&
        indexed.overlapSuggestions[0].leftName == L"B" &&
        indexed.overlapSuggestions[0].rightName == L"C" &&
        indexed.overlapSuggestions[0].commonIdCount == 5,
        "indexed analysis must retain rejected strong links");
}

std::wstring Serialize(const IdentityAnalysis& analysis)
{
    std::wostringstream out;
    for (const auto& group : analysis.exactGroups) {
        out << group.groupId << L':';
        for (const auto& name : group.names) out << name << L',';
        out << L';';
    }
    for (const auto& pair : analysis.overlapSuggestions) {
        out << pair.leftName << L',' << pair.rightName << L':' << pair.commonIdCount
            << L':' << pair.commonIdCountIsLowerBound << L';';
    }
    out << analysis.overlapSuggestionsTruncated;
    for (const auto& evidence : analysis.weakIdEvidence) {
        out << evidence.canonicalKey << L':';
        for (const auto& name : evidence.names) out << name << L',';
        out << L';';
    }
    return out.str();
}

void TestIndexedExactStrongWeakAndEmpty()
{
    const std::vector<AliasEntry> entries = {
        {L"exact-a", {L"x", L"x", L"y", L""}},
        {L"exact-b", {L"y", L"x"}},
        {L"strong-a", {L"1", L"2", L"3", L"4", L"5", L"a"}},
        {L"strong-b", {L"1", L"2", L"3", L"4", L"5", L"b"}},
        {L"weak-a", {L"w", L"w", L"w", L"w", L"a-only"}},
        {L"weak-b", {L"w", L"w", L"b-only"}},
        {L"three-a", {L"t1", L"t2", L"t3", L"ta"}},
        {L"three-b", {L"t1", L"t2", L"t3", L"tb"}},
        {L"empty-a", {}}, {L"empty-b", {L"", L""}}
    };
    const auto analysis = dnf::identity::AnalyzeIndexed(entries);
    Require(analysis.exactGroups.size() == 1, "only five-ID strong group qualifies");
    Require(analysis.overlapSuggestions.size() == 3 &&
        analysis.overlapSuggestions[0].commonIdCount == 2 &&
        analysis.overlapSuggestions[1].commonIdCount == 3 &&
        analysis.overlapSuggestions[2].commonIdCount == 1,
        "duplicate occurrences must not inflate one-ID or three-ID evidence");
    Require(Serialize(analysis) == Serialize(dnf::identity::Analyze(entries, SameId)),
        "uncapped indexed and legacy exact analysis should agree");
    Require(dnf::identity::AnalyzeIndexed({}).exactGroups.empty(), "empty library");
    Require(dnf::identity::AnalyzeIndexed({{L"a", {}}, {L"b", {L""}}})
        .exactGroups.empty(), "empty sets never auto-group");
}

void TestCanonicalizerAndLegacyEquivalence()
{
    const auto key = [](const std::wstring& id) {
        if (id == L"ignored") return std::wstring();
        std::wstring result = id;
        std::transform(result.begin(), result.end(), result.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        return result;
    };
    const std::vector<AliasEntry> entries = {
        {L"a", {L"USER|JobA", L"USER|JobA"}},
        {L"b", {L"user|joba"}},
        {L"c", {L"user|jobb"}},
        {L"d", {L"ignored"}}, {L"e", {L"ignored", L""}}
    };
    std::size_t calls = 0;
    const auto analysis = dnf::identity::AnalyzeIndexed(entries,
        [&](const std::wstring& id) { ++calls; return key(id); });
    Require(calls == 4, "canonicalize each distinct non-empty raw ID only once");
    Require(analysis.exactGroups.empty() && analysis.overlapSuggestions.size() == 1 &&
        analysis.overlapSuggestions[0].commonIdCount == 1,
        "caller controls normalization; declared jobs remain distinct; empty keys ignored");
    const std::vector<AliasEntry> distinct = {
        {L"a", {L"User|JobA"}}, {L"b", {L"user|JobA"}},
        {L"c", {L"User|JobB"}}
    };
    Require(dnf::identity::AnalyzeIndexed(distinct).exactGroups.empty(),
        "default keys preserve both case and declared job distinctions");
    const auto equivalent = [&](const std::wstring& a, const std::wstring& b) {
        return key(a) == key(b);
    };
    const auto legacy = dnf::identity::Analyze(distinct, equivalent);
    Require(legacy.exactGroups.empty() && legacy.overlapSuggestions.size() == 1,
        "legacy Analyze must retain arbitrary caller equivalence");
    Require(dnf::identity::Analyze(distinct, {}).exactGroups.empty(),
        "absent legacy equivalence retains the existing empty result");
}

void TestStableOrderingAndExactSignatureBoundaries()
{
    std::vector<AliasEntry> entries = {
        {L"Z", {L"ab", L"c", L"extra", L"extra2", L"extra3"}}, {L"Y", {L"c", L"ab", L"ab", L"extra", L"extra2", L"extra3"}},
        {L"X", {L"a", L"bc", L"extra", L"extra2", L"extra3"}}, {L"W", {L"bc", L"a", L"extra", L"extra2", L"extra3"}},
        {L"C", {L"5", L"6", L"7", L"8", L"bc5"}},
        {L"B", {L"1", L"2", L"3", L"4", L"ab5", L"5", L"6", L"7", L"8", L"bc5"}},
        {L"A", {L"1", L"2", L"3", L"4", L"ab5"}}
    };
    const auto expected = Serialize(dnf::identity::AnalyzeIndexed(entries));
    Require(dnf::identity::AnalyzeIndexed(entries).exactGroups.size() == 3,
        "exact set signatures must not collide through concatenation boundaries");
    std::mt19937 random(17);
    for (int iteration = 0; iteration < 20; ++iteration) {
        std::shuffle(entries.begin(), entries.end(), random);
        for (auto& entry : entries) std::shuffle(entry.ids.begin(), entry.ids.end(), random);
        Require(Serialize(dnf::identity::AnalyzeIndexed(entries)) == expected,
            "indexed groups, IDs, names and suggestions must have stable input-independent order");
        Require(Serialize(dnf::identity::Analyze(entries, SameId)) == expected,
            "legacy grouping must use the same stable ordering");
    }
}

void TestExactDuplicateCompression()
{
    std::vector<AliasEntry> entries;
    for (int i = 0; i < 10000; ++i) {
        entries.push_back({L"duplicate-" + std::to_wstring(i),
            {L"1", L"2", L"3", L"4", L"5", L"1"}});
    }
    std::size_t calls = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto analysis = dnf::identity::AnalyzeIndexed(entries,
        [&](const std::wstring& id) { ++calls; return id; });
    const auto ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    Require(calls == 5 && analysis.exactGroups.size() == 1 &&
        analysis.exactGroups[0].names.size() == entries.size(),
        "many exact duplicate sets must form one compressed group");
    Require(analysis.weakIdEvidence.empty() && !analysis.overlapSuggestionsTruncated,
        "exact duplicates must not consume the popular bucket pair budget");
    std::cout << "Benchmark exact 10000 names: " << ms << " ms, canonicalizations="
        << calls << '\n';
}

void TestPopularBucketsAreBoundedAndReported()
{
    std::vector<AliasEntry> entries;
    for (int i = 0; i < 10000; ++i) {
        entries.push_back({L"popular-" + std::to_wstring(i),
            {L"shared", L"unique-" + std::to_wstring(i)}});
    }
    const auto start = std::chrono::steady_clock::now();
    const auto analysis = dnf::identity::AnalyzeIndexed(entries);
    const auto ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    Require(analysis.exactGroups.empty(), "one popular ID must never auto-group");
    Require(analysis.overlapSuggestionsTruncated && analysis.weakIdEvidence.size() == 1 &&
        analysis.weakIdEvidence[0].canonicalKey == L"shared" &&
        analysis.weakIdEvidence[0].names.size() == entries.size(),
        "capped popular IDs must retain explicit evidence for every affected name");
    Require(analysis.overlapSuggestions.size() <= 100000,
        "popular IDs must not materialize quadratic suggestions");
    std::reverse(entries.begin(), entries.end());
    Require(Serialize(dnf::identity::AnalyzeIndexed(entries)) == Serialize(analysis),
        "capped evidence must also be stable under input reordering");
    std::cout << "Benchmark popular 10000 names: " << ms << " ms, evidence names="
        << analysis.weakIdEvidence[0].names.size() << '\n';
}

void TestCompressedSuggestionsRespectOutputBudget()
{
    std::vector<AliasEntry> entries;
    for (int i = 0; i < 400; ++i) {
        entries.push_back({L"a-" + std::to_wstring(i), {L"shared", L"a", L"a2", L"a3", L"a4"}});
        entries.push_back({L"b-" + std::to_wstring(i), {L"shared", L"b", L"b2", L"b3", L"b4"}});
    }
    const auto analysis = dnf::identity::AnalyzeIndexed(entries);
    Require(analysis.exactGroups.size() == 2, "compressed exact groups survive output capping");
    Require(analysis.overlapSuggestionsTruncated && !analysis.weakIdEvidence.empty() &&
        analysis.overlapSuggestions.size() <= 100000,
        "expanding duplicate clusters must not create unbounded suggestions");
}

void TestCappedCountsAreExplicitLowerBounds()
{
    std::vector<AliasEntry> entries = {
        {L"a", {L"popular", L"rare", L"own-a"}},
        {L"b", {L"popular", L"rare", L"own-b"}}
    };
    for (int i = 0; i < 300; ++i) {
        entries.push_back({L"other-" + std::to_wstring(i),
            {L"popular", L"unique-" + std::to_wstring(i)}});
    }
    const auto analysis = dnf::identity::AnalyzeIndexed(entries);
    Require(analysis.exactGroups.empty() && analysis.overlapSuggestions.size() == 1,
        "rare evidence must remain available even when a shared popular bucket is capped");
    const auto& pair = analysis.overlapSuggestions[0];
    Require(pair.leftName == L"a" && pair.rightName == L"b" &&
        pair.commonIdCount == 1 && pair.commonIdCountIsLowerBound,
        "capped shared counts must be identified as lower bounds, not exact counts");
    Require(analysis.overlapSuggestionsTruncated && analysis.weakIdEvidence.size() == 1,
        "the omitted contribution must also appear in compact weak evidence");
}

void TestGlobalPairBudgetPreservesEvidence()
{
    std::vector<std::wstring> shared;
    for (int id = 0; id < 220; ++id) shared.push_back(L"shared-" + std::to_wstring(id));
    std::vector<AliasEntry> entries;
    for (int i = 0; i < 96; ++i) {
        auto ids = shared;
        ids.push_back(L"unique-" + std::to_wstring(i));
        entries.push_back({L"name-" + std::to_wstring(i), std::move(ids)});
    }
    for (int i = 0; i < 96; ++i) entries.push_back({L"later-" + std::to_wstring(i),
        {L"zz1", L"zz2", L"zz3", L"zz4", L"zz5", L"own-" + std::to_wstring(i)}});
    const auto analysis = dnf::identity::AnalyzeIndexed(entries);
    Require(analysis.exactGroups.size() == 2 && analysis.exactGroups[0].names.size() == 96 &&
        analysis.exactGroups[1].names.size() == 96,
        "the suggestion pair budget must not discard later strong groups");
    Require(analysis.overlapSuggestionsTruncated && analysis.weakIdEvidence.size() == 6 &&
        analysis.weakIdEvidence[0].names.size() == 96,
        "many small buckets must obey the global budget and retain omitted evidence");
    std::reverse(entries.begin(), entries.end());
    for (auto& entry : entries) std::reverse(entry.ids.begin(), entry.ids.end());
    Require(Serialize(dnf::identity::AnalyzeIndexed(entries)) == Serialize(analysis),
        "global budget decisions must be deterministic under all input order changes");
}

void TestPopularStrongBucketsAreComplete()
{
    {
        std::vector<AliasEntry> entries;
        for (int i = 0; i < 257; ++i) {
            AliasEntry entry{L"A-" + std::to_wstring(i), {}};
            auto& ids = entry.ids;
            for (int id = 1; id <= 5; ++id) ids.push_back(L"shared-" + std::to_wstring(id));
            ids.push_back(L"own-" + std::to_wstring(i));
            entries.push_back(std::move(entry));
        }
        const auto result = dnf::identity::AnalyzeIndexed(entries);
        Require(result.exactGroups.size() == 1 && result.exactGroups[0].names.size() == 257,
            "257 distinct strong sets must auto-group despite suggestion bucket capping");
        Require(result.overlapSuggestions.size() <= 100000, "dense suggestions stay bounded");
        const auto reference = dnf::identity::Analyze(entries, SameId);
        Require(result.exactGroups[0].names == reference.exactGroups[0].names,
            "complete indexed clique agrees with exhaustive original-evidence analysis");
        std::reverse(entries.begin(), entries.end());
        Require(Serialize(result) == Serialize(dnf::identity::AnalyzeIndexed(entries)), "dense grouping is deterministic");

        AliasEntry bridge{L"B", {}};
        auto& bridgeIds = bridge.ids;
        for (int id = 1; id <= 5; ++id) {
            bridgeIds.push_back(L"shared-" + std::to_wstring(id));
            bridgeIds.push_back(L"right-" + std::to_wstring(id));
        }
        entries.push_back(bridge);
        for (int i = 0; i < 257; ++i) {
            AliasEntry entry{L"C-" + std::to_wstring(i), {}};
            auto& ids = entry.ids;
            for (int id = 1; id <= 5; ++id) ids.push_back(L"right-" + std::to_wstring(id));
            ids.push_back(L"right-own-" + std::to_wstring(i));
            entries.push_back(std::move(entry));
        }
        const auto chain = dnf::identity::AnalyzeIndexed(entries);
        Require(chain.exactGroups.size() == 2 && chain.exactGroups[0].names.size() == 258 && chain.exactGroups[1].names.size() == 257,
            "a dense bridge must not turn original-evidence cliques into a union chain");
        const auto exhaustive = dnf::identity::Analyze(entries, SameId);
        Require(chain.exactGroups[0].names == exhaustive.exactGroups[0].names && chain.exactGroups[1].names == exhaustive.exactGroups[1].names,
            "dense chain partition agrees with exhaustive pairwise evidence");
    }
}

void TestAllPairsWithinGroupsAndNoLostSuggestions()
{
    std::mt19937 random(73);
    for (int iteration = 0; iteration < 40; ++iteration) {
        std::vector<AliasEntry> entries;
        std::vector<std::set<std::wstring>> sets;
        for (int i = 0; i < 16; ++i) {
            AliasEntry entry{L"name-" + std::to_wstring(i), {}};
            for (int id = 0; id < 12; ++id) {
                if (random() % 2) entry.ids.push_back(L"id-" + std::to_wstring(id));
            }
            sets.emplace_back(entry.ids.begin(), entry.ids.end());
            entries.push_back(std::move(entry));
        }
        const auto analysis = dnf::identity::AnalyzeIndexed(entries);
        Require(!analysis.overlapSuggestionsTruncated, "small random cases must be exhaustive");
        Require(Serialize(analysis) == Serialize(dnf::identity::Analyze(entries, SameId)),
            "indexed and arbitrary-equivalence paths must agree on random exact-key inputs");
        for (std::size_t i = 0; i < entries.size(); ++i) {
            for (std::size_t j = i + 1; j < entries.size(); ++j) {
                std::size_t common = 0;
                for (const auto& id : sets[i]) common += sets[j].count(id);
                bool grouped = false;
                for (const auto& group : analysis.exactGroups) {
                    grouped = grouped || (std::find(group.names.begin(), group.names.end(),
                        entries[i].name) != group.names.end() &&
                        std::find(group.names.begin(), group.names.end(), entries[j].name) != group.names.end());
                }
                if (grouped) {
                    Require(common >= 5,
                        "every pair in every emitted group must be independently compatible");
                }
                else if (common > 0) {
                    const auto left = (std::min)(entries[i].name, entries[j].name);
                    const auto right = (std::max)(entries[i].name, entries[j].name);
                    Require(std::any_of(analysis.overlapSuggestions.begin(),
                        analysis.overlapSuggestions.end(), [&](const auto& pair) {
                            return pair.leftName == left && pair.rightName == right &&
                                pair.commonIdCount == common;
                        }), "every positive ungrouped pair must remain a counted suggestion");
                }
            }
        }
    }
}

void BenchmarkSparseLibrary()
{
    std::vector<AliasEntry> entries;
    for (int i = 0; i < 10000; ++i) {
        const auto id = std::to_wstring(i);
        entries.push_back({L"name-" + id, {L"a-" + id, L"b-" + id,
            L"c-" + id, L"pair-" + std::to_wstring(i / 2)}});
    }
    const auto fingerprint = dnf::identity::ComputeAliasEntriesFingerprint(entries);
    std::size_t calls = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto analysis = dnf::identity::AnalyzeIndexed(entries,
        [&](const std::wstring& id) { ++calls; return id; });
    const auto ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    Require(calls == 35000 && analysis.exactGroups.empty() &&
        analysis.overlapSuggestions.size() == 5000 && !analysis.overlapSuggestionsTruncated,
        "sparse 10k analysis must only return the actual 5000 overlapping pairs");
    Require(dnf::identity::ComputeAliasEntriesFingerprint(entries) == fingerprint,
        "analysis must not mutate the input library");
    std::cout << "Benchmark sparse 10000 names / 40000 IDs: " << ms
        << " ms, canonicalizations=" << calls << ", suggestions="
        << analysis.overlapSuggestions.size() << '\n';
}

void TestThreeSharedIdsStayIndependent()
{
    const std::vector<AliasEntry> entries = {
        {L"选手甲", {L"A", L"B", L"C", L"甲专属"}},
        {L"选手乙", {L"A", L"B", L"C", L"乙专属"}}
    };

    const IdentityAnalysis analysis = dnf::identity::Analyze(entries, SameId);
    Require(analysis.exactGroups.empty() && analysis.overlapSuggestions.size() == 1,
        "three shared IDs must stay independent under the new policy");
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
    TestPopularStrongBucketsAreComplete();
    for (const auto indexed : {false, true}) {
        const std::vector<AliasEntry> two = {{L"A", {L"1", L"2", L"1"}}, {L"B", {L"2", L"1", L"2"}}};
        Require((indexed ? dnf::identity::AnalyzeIndexed(two) : dnf::identity::Analyze(two, SameId)).exactGroups.empty(),
            "identical two-ID sets must not bypass the five-game-ID threshold");
        for (int count = 1; count <= 5; ++count) {
            std::vector<std::wstring> ids;
            for (int i = 1; i <= count; ++i) { ids.push_back(std::to_wstring(i)); ids.push_back(std::to_wstring(i)); }
            std::vector<AliasEntry> pair = {{L"A", ids}, {L"B", ids}};
            const auto exact = indexed ? dnf::identity::AnalyzeIndexed(pair) : dnf::identity::Analyze(pair, SameId);
            Require(exact.exactGroups.size() == (count == 5 ? 1u : 0u), "distinct game threshold also applies to exact small sets");
            if (count < 5) Require(exact.overlapSuggestions[0].commonIdCount == static_cast<std::size_t>(count), "duplicates count only once");
        }
    }
    TestExactGroupsIgnoreEmptyEntries();
    TestPartialOverlapIsOnlySuggestion();
    TestStrongOverlapAutoGroups();
    TestChainDoesNotAutoMerge();
    TestCurrentAutoGroupPolicyVersion();
    TestThreeSharedIdsStayIndependent();
    TestUnionDeduplicatesIDs();
    TestAliasEntryFingerprintChangesWhenIDsChange();
    TestIndexedExactStrongWeakAndEmpty();
    TestCanonicalizerAndLegacyEquivalence();
    TestStableOrderingAndExactSignatureBoundaries();
    TestAllPairsWithinGroupsAndNoLostSuggestions();
    TestExactDuplicateCompression();
    TestPopularBucketsAreBoundedAndReported();
    TestCompressedSuggestionsRespectOutputBudget();
    TestCappedCountsAreExplicitLowerBounds();
    TestGlobalPairBudgetPreservesEvidence();
    BenchmarkSparseLibrary();
    std::cout << "Player identity group tests passed.\n";
    return 0;
}
