#include "CloudMatchSync.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <sstream>
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

bool HasExactKeysWithOptionalRecentEvents(const json& value,
    std::initializer_list<const char*> keys)
{
    if (!value.is_object()) return false;
    const bool hasRecentEvents = value.contains("recentEvents");
    if (value.size() != keys.size() + (hasRecentEvents ? 1u : 0u)) return false;
    for (const char* key : keys) {
        if (!value.contains(key)) return false;
    }
    return !hasRecentEvents || value["recentEvents"].is_array();
}

bool IsSafeRecentEventText(const json& value)
{
    if (!value.is_string()) return false;
    const std::string& text = value.get_ref<const std::string&>();
    return text.size() <= 1024 && text.find('\r') == std::string::npos &&
        text.find('\n') == std::string::npos && text.find('\0') == std::string::npos;
}

bool ValidateRecentEvents(const json& cloudSnapshot)
{
    const auto recent = cloudSnapshot.find("recentEvents");
    if (recent == cloudSnapshot.end()) return true;
    if (!recent->is_array() || recent->size() > 10) return false;
    for (const auto& event : *recent) {
        if (!HasExactKeys(event, { "time", "killer", "dead", "status" })) {
            return false;
        }
        for (const char* key : { "time", "killer", "dead", "status" }) {
            if (!IsSafeRecentEventText(event[key])) return false;
        }
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
        if (!HasExactKeysWithOptionalRecentEvents(cloudSnapshot,
            { "schemaVersion", "clientRevision", "clientTime", "changeSource",
              "syncedFrom", "redScore", "blueScore", "redPlayers", "bluePlayers",
              "redPickFirst", "teamsFlipped", "outputSeatLabel", "lastKillTeam" })) {
            return fail("invalid_snapshot");
        }
    }
    else if (!HasExactKeysWithOptionalRecentEvents(cloudSnapshot,
        { "schemaVersion", "clientRevision", "clientTime", "changeSource",
          "redScore", "blueScore", "redPlayers", "bluePlayers", "redPickFirst",
          "teamsFlipped", "outputSeatLabel", "lastKillTeam" })) {
        return fail("invalid_snapshot");
    }
    if (!ValidateRecentEvents(cloudSnapshot)) return fail("invalid_snapshot");

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

bool DnfResolveCloudMatchRelativeSwap(const json& members,
    const std::string& localDeviceId, const std::string& targetDeviceId,
    bool& relativeSwap, std::string& errorCode)
{
    relativeSwap = false;
    errorCode.clear();
    if (!members.is_array() || members.size() > 512 || localDeviceId.empty() ||
        targetDeviceId.empty()) {
        errorCode = "invalid_members";
        return false;
    }

    bool localFound = false;
    bool targetFound = false;
    bool localSwapped = false;
    bool targetSwapped = false;
    for (const auto& member : members) {
        if (!member.is_object()) continue;
        const auto id = member.find("deviceId");
        const auto swapped = member.find("swapped");
        if (id == member.end() || !id->is_string() ||
            swapped == member.end() || !swapped->is_boolean()) {
            continue;
        }
        const std::string& deviceId = id->get_ref<const std::string&>();
        if (deviceId == localDeviceId) {
            localFound = true;
            localSwapped = swapped->get<bool>();
        }
        if (deviceId == targetDeviceId) {
            targetFound = true;
            targetSwapped = swapped->get<bool>();
        }
    }
    if (!localFound) {
        errorCode = "local_member_missing";
        return false;
    }
    if (!targetFound) {
        errorCode = "target_member_missing";
        return false;
    }
    relativeSwap = localSwapped != targetSwapped;
    return true;
}

bool DnfIsCloudMatchPreviewCurrent(const DnfCloudMatchPreviewBinding& binding,
    const DnfCloudMatchCurrentState& current) noexcept
{
    return current.connected && current.roomConfirmed &&
        !binding.roomId.empty() && binding.roomId == current.roomId &&
        binding.connectionGeneration != 0 &&
        binding.connectionGeneration == current.connectionGeneration &&
        binding.roomRevision != 0 && binding.roomRevision == current.roomRevision &&
        !binding.requestId.empty() && binding.requestId == current.requestId &&
        !binding.deviceId.empty() && binding.deviceId == current.deviceId &&
        binding.snapshotRevision != 0 &&
        binding.snapshotRevision == current.snapshotRevision;
}

std::string DnfCloudMatchContentHash(const json& snapshot)
{
    const std::string serialized = snapshot.dump();
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : serialized) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

bool DnfCanCloudMatchUndo(const DnfCloudMatchUndoGuard& guard,
    std::uint64_t currentEpoch, const std::string& currentHash,
    const std::string& currentRoomId,
    std::uint64_t currentConnectionGeneration) noexcept
{
    return guard.available && guard.postApplyEpoch != 0 &&
        guard.postApplyEpoch == currentEpoch && !guard.postApplyHash.empty() &&
        guard.postApplyHash == currentHash && !guard.roomId.empty() &&
        guard.roomId == currentRoomId && guard.connectionGeneration != 0 &&
        guard.connectionGeneration == currentConnectionGeneration;
}
