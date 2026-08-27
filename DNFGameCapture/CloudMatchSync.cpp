#include "CloudMatchSync.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <string_view>

using nlohmann::json;

namespace {

constexpr std::size_t kMaxCloudSnapshotBytes = 65536;
constexpr std::uint64_t kMaxSafeInteger = 9007199254740991ULL;

bool HasExactKeys(const json& value, std::initializer_list<const char*> keys)
{
    if (!value.is_object() || value.size() != keys.size()) return false;
    for (const char* key : keys) {
        if (!value.contains(key)) return false;
    }
    return true;
}

bool ReadUnsigned(const json& value, const char* key, std::uint64_t minimum,
    std::uint64_t maximum, std::uint64_t& output)
{
    const auto found = value.find(key);
    if (found == value.end() ||
        (!found->is_number_integer() && !found->is_number_unsigned())) {
        return false;
    }
    if (found->is_number_unsigned()) {
        output = found->get<std::uint64_t>();
    }
    else {
        const std::int64_t signedValue = found->get<std::int64_t>();
        if (signedValue < 0) return false;
        output = static_cast<std::uint64_t>(signedValue);
    }
    return output >= minimum && output <= maximum;
}

bool IsDeviceId(const std::string& value)
{
    if (value.size() < 8 || value.size() > 128) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
    });
}

bool ConvertPlayer(const json& cloudPlayer, int team,
    const DnfCloudMatchNameNormalizer& normalizeName, json& teamPlayer)
{
    if (!HasExactKeys(cloudPlayer,
        { "mainName", "aliases", "kills", "deaths", "ak", "streak" })) {
        return false;
    }
    if (!cloudPlayer["mainName"].is_string() ||
        !cloudPlayer["aliases"].is_array() || cloudPlayer["aliases"].size() > 32) {
        return false;
    }

    std::string mainName;
    if (!normalizeName(cloudPlayer["mainName"].get<std::string>(), mainName)) {
        return false;
    }
    std::uint64_t kills = 0;
    std::uint64_t deaths = 0;
    std::uint64_t ak = 0;
    std::uint64_t streak = 0;
    if (!ReadUnsigned(cloudPlayer, "kills", 0, 999, kills) ||
        !ReadUnsigned(cloudPlayer, "deaths", 0, 999, deaths) ||
        !ReadUnsigned(cloudPlayer, "ak", 0, 999, ak) ||
        !ReadUnsigned(cloudPlayer, "streak", 0, 999, streak)) {
        return false;
    }

    json aliases = json::array();
    std::set<std::string> uniqueAliases;
    for (const auto& aliasValue : cloudPlayer["aliases"]) {
        if (!aliasValue.is_string()) return false;
        std::string alias;
        if (!normalizeName(aliasValue.get<std::string>(), alias) || alias == mainName) {
            return false;
        }
        if (uniqueAliases.insert(alias).second) aliases.push_back(std::move(alias));
    }

    teamPlayer = {
        { "team", team }, { "name", std::move(mainName) },
        { "aliases", std::move(aliases) },
        { "kills", kills }, { "deaths", deaths },
        { "akCount", ak }, { "currentStreak", streak }
    };
    return true;
}

void AddDifference(json& group, const char* field, const json& local,
    const json& remote, int seat = -1)
{
    if (local == remote) return;
    json item = {
        { "field", field }, { "local", local }, { "remote", remote }
    };
    if (seat >= 0) item["seat"] = seat;
    group["items"].push_back(std::move(item));
}

json DifferenceGroup(const char* id)
{
    return { { "id", id }, { "items", json::array() } };
}

} // namespace

bool DnfConvertCloudMatchSnapshot(const json& cloudSnapshot,
    std::uint64_t expectedClientRevision, bool swapped,
    const DnfCloudMatchNameNormalizer& normalizeName, json& teamSnapshot,
    std::string& errorCode)
{
    teamSnapshot = json::object();
    errorCode.clear();
    auto fail = [&](const char* code) {
        errorCode = code;
        return false;
    };
    if (!normalizeName || !cloudSnapshot.is_object() ||
        cloudSnapshot.dump().size() > kMaxCloudSnapshotBytes) {
        return fail("invalid_snapshot");
    }

    const auto sourceFound = cloudSnapshot.find("changeSource");
    if (sourceFound == cloudSnapshot.end() || !sourceFound->is_string()) {
        return fail("invalid_snapshot");
    }
    const std::string changeSource = sourceFound->get<std::string>();
    const bool cloudSync = changeSource == "cloud_sync";
    const bool allowedSource = changeSource == "ocr" || changeSource == "manual" ||
        changeSource == "local_restore" || cloudSync;
    if (!allowedSource) return fail("invalid_snapshot");

    if (cloudSync) {
        if (!HasExactKeys(cloudSnapshot,
            { "schemaVersion", "clientRevision", "clientTime", "changeSource",
              "syncedFrom", "redScore", "blueScore", "redPlayers", "bluePlayers",
              "redPickFirst", "teamsFlipped", "outputSeatLabel", "lastKillTeam" })) {
            return fail("invalid_snapshot");
        }
    }
    else if (!HasExactKeys(cloudSnapshot,
        { "schemaVersion", "clientRevision", "clientTime", "changeSource",
          "redScore", "blueScore", "redPlayers", "bluePlayers", "redPickFirst",
          "teamsFlipped", "outputSeatLabel", "lastKillTeam" })) {
        return fail("invalid_snapshot");
    }

    std::uint64_t schemaVersion = 0;
    std::uint64_t clientRevision = 0;
    std::uint64_t clientTime = 0;
    std::uint64_t redScore = 0;
    std::uint64_t blueScore = 0;
    if (!ReadUnsigned(cloudSnapshot, "schemaVersion", 1, 1, schemaVersion) ||
        !ReadUnsigned(cloudSnapshot, "clientRevision", 1, kMaxSafeInteger,
            clientRevision) ||
        !ReadUnsigned(cloudSnapshot, "clientTime", 0, kMaxSafeInteger, clientTime) ||
        !ReadUnsigned(cloudSnapshot, "redScore", 0, 999, redScore) ||
        !ReadUnsigned(cloudSnapshot, "blueScore", 0, 999, blueScore) ||
        expectedClientRevision == 0 || clientRevision != expectedClientRevision) {
        return fail("invalid_snapshot");
    }
    if (!cloudSnapshot["redPlayers"].is_array() ||
        cloudSnapshot["redPlayers"].size() != 4 ||
        !cloudSnapshot["bluePlayers"].is_array() ||
        cloudSnapshot["bluePlayers"].size() != 4 ||
        !cloudSnapshot["redPickFirst"].is_boolean() ||
        !cloudSnapshot["teamsFlipped"].is_boolean() ||
        !cloudSnapshot["outputSeatLabel"].is_boolean() ||
        !cloudSnapshot["lastKillTeam"].is_string()) {
        return fail("invalid_snapshot");
    }
    const std::string lastKillTeam = cloudSnapshot["lastKillTeam"].get<std::string>();
    if (lastKillTeam != "red" && lastKillTeam != "blue" && !lastKillTeam.empty()) {
        return fail("invalid_snapshot");
    }
    if (cloudSync) {
        const json& syncedFrom = cloudSnapshot["syncedFrom"];
        std::uint64_t sourceRevision = 0;
        if (!HasExactKeys(syncedFrom, { "deviceId", "revision" }) ||
            !syncedFrom["deviceId"].is_string() ||
            !IsDeviceId(syncedFrom["deviceId"].get<std::string>()) ||
            !ReadUnsigned(syncedFrom, "revision", 1, kMaxSafeInteger,
                sourceRevision)) {
            return fail("invalid_snapshot");
        }
    }

    const json& redPlayers = cloudSnapshot[swapped ? "bluePlayers" : "redPlayers"];
    const json& bluePlayers = cloudSnapshot[swapped ? "redPlayers" : "bluePlayers"];
    teamSnapshot = {
        { "version", 1 },
        { "redScore", swapped ? blueScore : redScore },
        { "blueScore", swapped ? redScore : blueScore },
        { "redPickMode", cloudSnapshot["redPickFirst"].get<bool>() != swapped ?
            "first" : "second" },
        { "isFlipped", cloudSnapshot["teamsFlipped"] },
        { "outputSeatLabelToKillFile", cloudSnapshot["outputSeatLabel"] },
        { "lastKillerTeam", lastKillTeam.empty() ? -1 :
            ((lastKillTeam == "red") != swapped ? 0 : 1) },
        { "players", json::array() }
    };
    for (int index = 0; index < 4; ++index) {
        json player;
        if (!ConvertPlayer(redPlayers[index], 0, normalizeName, player)) {
            teamSnapshot = json::object();
            return fail("invalid_snapshot");
        }
        teamSnapshot["players"].push_back(std::move(player));
    }
    for (int index = 0; index < 4; ++index) {
        json player;
        if (!ConvertPlayer(bluePlayers[index], 1, normalizeName, player)) {
            teamSnapshot = json::object();
            return fail("invalid_snapshot");
        }
        teamSnapshot["players"].push_back(std::move(player));
    }
    return true;
}

json DnfBuildCloudMatchPreview(const json& localTeamSnapshot,
    const json& remoteTeamSnapshot, bool swapped)
{
    json score = DifferenceGroup("score");
    AddDifference(score, "redScore", localTeamSnapshot["redScore"],
        remoteTeamSnapshot["redScore"]);
    AddDifference(score, "blueScore", localTeamSnapshot["blueScore"],
        remoteTeamSnapshot["blueScore"]);

    json roster = DifferenceGroup("roster");
    json stats = DifferenceGroup("stats");
    for (int index = 0; index < 8; ++index) {
        const auto& local = localTeamSnapshot["players"][index];
        const auto& remote = remoteTeamSnapshot["players"][index];
        AddDifference(roster, "mainName", local["name"], remote["name"], index);
        AddDifference(roster, "aliases", local["aliases"], remote["aliases"], index);
        AddDifference(stats, "kills", local["kills"], remote["kills"], index);
        AddDifference(stats, "deaths", local["deaths"], remote["deaths"], index);
        AddDifference(stats, "ak", local["akCount"], remote["akCount"], index);
        AddDifference(stats, "streak", local["currentStreak"],
            remote["currentStreak"], index);
    }

    json state = DifferenceGroup("state");
    AddDifference(state, "redPickFirst", localTeamSnapshot["redPickMode"],
        remoteTeamSnapshot["redPickMode"]);
    AddDifference(state, "teamsFlipped", localTeamSnapshot["isFlipped"],
        remoteTeamSnapshot["isFlipped"]);
    AddDifference(state, "outputSeatLabel",
        localTeamSnapshot["outputSeatLabelToKillFile"],
        remoteTeamSnapshot["outputSeatLabelToKillFile"]);
    AddDifference(state, "lastKillTeam", localTeamSnapshot["lastKillerTeam"],
        remoteTeamSnapshot["lastKillerTeam"]);

    const std::size_t differenceCount = score["items"].size() +
        roster["items"].size() + stats["items"].size() + state["items"].size();
    return {
        { "swapped", swapped }, { "differenceCount", differenceCount },
        { "groups", json::array({ std::move(score), std::move(roster),
            std::move(stats), std::move(state) }) }
    };
}
