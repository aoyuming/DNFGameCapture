#include "../PlayerIdentityOcrCache.h"
#include "../OcrMatchRanking.h"
#include "../OcrTeamMapping.h"

#include <cstdlib>
#include <iostream>

namespace {
using namespace dnf::identity;
using namespace dnf::player_library;

void Require(bool value, const char* message)
{
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::shared_ptr<Snapshot> MakeSnapshot()
{
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->revision = 1;
    snapshot->identifiers = {
        {1, IdentifierKind::Game, CanonicalKey(L"Game#Job", IdentifierKind::Game), L"Game#Job", {}}
    };
    PlayerEntity first;
    first.entityId = 1;
    first.names = {L"Alice", L"AliceAlias"};
    first.gameIds = {1};
    PlayerEntity second;
    second.entityId = 2;
    second.names = {L"Bob"};
    snapshot->entities = {first, second};
    BuildSnapshotViews(*snapshot);
    return snapshot;
}

std::array<OcrActivePlayer, 8> Roster()
{
    std::array<OcrActivePlayer, 8> players{};
    players[0] = {L"Alice", 0, {L"Game#Job", L" Game # Job "}};
    return players;
}

OcrGameIdMetadata Parse(const std::wstring& value)
{
    OcrGameIdMetadata result;
    result.matchName = value;
    result.fullMatchName = value;
    return result;
}

struct RankingCandidate {
    int playerIndex = -1;
    int team = -1;
    int score = -1;
    int realIdLength = 0;
};

void TestNormalizedCandidatesAndCacheLifetime()
{
    OcrLookupCache cache;
    auto snapshot = MakeSnapshot();
    auto roster = Roster();
    std::size_t parses = 0;
    const auto parse = [&](const auto& value) { ++parses; return Parse(value); };
    Require(cache.Prepare(snapshot, roster, parse), "initial scope must prepare candidates");
    Require(cache.GameCandidates().size() == 1 && parses == 1,
        "normalized duplicate IDs must be parsed once and not duplicate temporal candidates");
    Require(cache.GameCandidates()[0].name == L"Game#Job" &&
        cache.GameCandidates()[0].playerIndex == 0, "candidate must preserve the real active alias");
    Require(!cache.Prepare(snapshot, roster, parse) && parses == 1,
        "unchanged snapshot and roster must reuse parsed metadata");
    std::weak_ptr<const Snapshot> old = snapshot;
    snapshot.reset();
    Require(!old.expired(), "cache must pin its immutable snapshot lifetime");
    cache.Prepare(MakeSnapshot(), roster, parse);
    Require(old.expired(), "a replacement snapshot must release the old lifetime");
}

void TestTemporaryAliasRemovalRemainsAuthoritative()
{
    OcrLookupCache cache;
    auto snapshot = MakeSnapshot();
    auto roster = Roster();
    cache.Prepare(snapshot, roster, Parse);
    roster[0].gameIds.clear();
    Require(cache.Prepare(snapshot, roster, Parse) && cache.GameCandidates().empty(),
        "temporarily removed aliases must disappear even while the library still owns them");
    auto refreshed = MakeSnapshot();
    refreshed->revision++;
    cache.Prepare(refreshed, roster, Parse);
    Require(cache.GameCandidates().empty(), "snapshot refresh must not restore removed game candidates");
    roster[0].gameIds = {L"Local#JobA", L"local#JobA", L"Local#JobB"};
    cache.Prepare({}, roster, Parse);
    Require(cache.GameCandidates().size() == 3,
        "local aliases remain usable without a snapshot and retain case/job distinctions");
}

void TestScopeChangesAndReset()
{
    OcrLookupCache cache;
    auto snapshot = MakeSnapshot();
    auto roster = Roster();
    cache.Prepare(snapshot, roster, Parse);
    roster[0].team = 1;
    Require(cache.Prepare(snapshot, roster, Parse), "team change invalidates candidates");
    roster[7] = {L"Bob", 0, {L"Game#Job"}};
    cache.Prepare(snapshot, roster, Parse);
    Require(cache.GameCandidates().size() == 2, "shared IDs preserve both active candidates");
    roster[7].team = -1;
    cache.Prepare(snapshot, roster, Parse);
    Require(cache.GameCandidates().size() == 1, "inactive slots are not OCR candidates");
    snapshot->revision++;
    Require(cache.Prepare(snapshot, roster, Parse), "revision change invalidates metadata");
    cache.Reset();
    Require(cache.GameCandidates().empty(), "reset discards cached candidates");
    Require(cache.Prepare(snapshot, roster, Parse), "same roster rebuilds after reset");
    roster[0].name.clear();
    cache.Prepare(snapshot, roster, Parse);
    Require(cache.GameCandidates().empty(), "unnamed slots cannot be identified");
}

void TestPhysicalSideTeamMappingWithFlip()
{
    using dnf::identity::PhysicalSideToTeam;

    Require(PhysicalSideToTeam(0, false) == 0 && PhysicalSideToTeam(1, false) == 1,
        "without red-blue flip, left and right panels map to their corresponding teams");
    Require(PhysicalSideToTeam(0, true) == 1 && PhysicalSideToTeam(1, true) == 0,
        "red-blue flip must reverse physical-panel team candidates");
}

void TestHighestScoreIsPerPlayerAndFallbackExpandsScope()
{
    std::vector<RankingCandidate> candidates;
    KeepBestOcrScorePerPlayer(candidates, {0, 0, 71, 4});
    KeepBestOcrScorePerPlayer(candidates, {0, 0, 86, 4});
    KeepBestOcrScorePerPlayer(candidates, {1, 0, 78, 5});
    KeepBestOcrScorePerPlayer(candidates, {4, 1, 92, 6});

    Require(candidates.size() == 3,
        "repeated OCR frames for the same player must update that player, not create a second candidate");
    const auto teamTop = TopTwoOcrPlayers(candidates, 0);
    Require(teamTop.first >= 0 && candidates[teamTop.first].playerIndex == 0 &&
        teamTop.second >= 0 && candidates[teamTop.second].playerIndex == 1,
        "the constrained phase must rank the highest players in the expected team");
    const auto allPlayersTop = TopTwoOcrPlayers(candidates);
    Require(allPlayersTop.first >= 0 && candidates[allPlayersTop.first].playerIndex == 4,
        "the final fallback must rank all players only after the team-scoped result");
    const auto oppositeTeamTop = TopTwoOcrPlayers(candidates, -1, 1);
    Require(oppositeTeamTop.first >= 0 && candidates[oppositeTeamTop.first].playerIndex == 0,
        "global fallback must be able to exclude the already-resolved opponent team");
}
}

int main()
{
    TestNormalizedCandidatesAndCacheLifetime();
    TestTemporaryAliasRemovalRemainsAuthoritative();
    TestScopeChangesAndReset();
    TestPhysicalSideTeamMappingWithFlip();
    TestHighestScoreIsPerPlayerAndFallbackExpandsScope();
    std::cout << "Player identity OCR tests passed.\n";
}
