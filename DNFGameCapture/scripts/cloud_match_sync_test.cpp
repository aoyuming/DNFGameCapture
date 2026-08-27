#include "../CloudMatchSync.h"

#include <cstdlib>
#include <iostream>
#include <string>

using nlohmann::json;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

bool NormalizeTestName(const std::string& input, std::string& output)
{
    const auto first = input.find_first_not_of(" \t\r\n");
    const auto last = input.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || input.find('\0') != std::string::npos) return false;
    output = input.substr(first, last - first + 1);
    return output.size() <= 256 && output.find('\r') == std::string::npos &&
        output.find('\n') == std::string::npos;
}

json Player(const std::string& name, int value)
{
    return {
        { "mainName", name }, { "aliases", json::array({ name + " Alt" }) },
        { "kills", value }, { "deaths", value + 1 },
        { "ak", value + 2 }, { "streak", value + 3 }
    };
}

json Snapshot()
{
    json snapshot = {
        { "schemaVersion", 1 }, { "clientRevision", 17 },
        { "clientTime", 1700000000 }, { "changeSource", "ocr" },
        { "redScore", 3 }, { "blueScore", 2 },
        { "redPlayers", json::array() }, { "bluePlayers", json::array() },
        { "redPickFirst", true }, { "teamsFlipped", false },
        { "outputSeatLabel", true }, { "lastKillTeam", "red" }
    };
    for (int i = 0; i < 4; ++i) {
        snapshot["redPlayers"].push_back(Player("Red " + std::to_string(i + 1), i));
        snapshot["bluePlayers"].push_back(Player("Blue " + std::to_string(i + 1), i + 10));
    }
    return snapshot;
}

void TestStrictConversionAndSwappedOrientation()
{
    json converted;
    std::string error;
    const json source = Snapshot();
    Require(DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "legal cloud snapshot should convert");
    Require(converted["redScore"] == 3 && converted["blueScore"] == 2,
        "normal orientation should preserve scores");
    Require(converted["players"][0]["name"] == "Red 1" &&
        converted["players"][4]["name"] == "Blue 1",
        "normal orientation should preserve team rosters");

    Require(DnfConvertCloudMatchSnapshot(source, 17, true, NormalizeTestName,
        converted, error), "swapped cloud snapshot should convert");
    Require(converted["redScore"] == 2 && converted["blueScore"] == 3,
        "swapped orientation should exchange scores");
    Require(converted["players"][0]["name"] == "Blue 1" &&
        converted["players"][4]["name"] == "Red 1",
        "swapped orientation should exchange complete rosters");
    Require(converted["redPickMode"] == "second" &&
        converted["lastKillerTeam"] == 1,
        "swapped orientation should transform team-relative state");
    Require(converted["isFlipped"] == false &&
        converted["outputSeatLabelToKillFile"] == true,
        "swapped orientation should preserve direct metadata state");
}

void TestStrictCloudSchemaRejections()
{
    json converted;
    std::string error;
    json source = Snapshot();
    source["extra"] = true;
    Require(!DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "unknown snapshot field must be rejected");

    source = Snapshot();
    source["redPlayers"].erase(source["redPlayers"].end() - 1);
    Require(!DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "seven-player snapshot must be rejected");

    source = Snapshot();
    source["redPlayers"][0]["kills"] = 1000;
    Require(!DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "out-of-range stat must be rejected");

    source = Snapshot();
    source["redPlayers"][0]["unknown"] = 1;
    Require(!DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "unknown player field must be rejected");

    source = Snapshot();
    source["changeSource"] = "cloud_sync";
    Require(!DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "cloud_sync without syncedFrom must be rejected");

    source["syncedFrom"] = { { "deviceId", "source-device-0001" },
        { "revision", 16 } };
    Require(DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "cloud_sync with valid source must convert");
    source["syncedFrom"]["token"] = "must-not-pass";
    Require(!DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "unknown syncedFrom field must be rejected");

    source = Snapshot();
    source["padding"] = std::string(65536, 'x');
    Require(!DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error), "snapshot over 64KB must be rejected");
}

void TestSnapshotRevisionCorrelation()
{
    json converted;
    std::string error;
    json source = Snapshot();
    source["clientRevision"] = 18;
    Require(!DnfConvertCloudMatchSnapshot(source, 17, false, NormalizeTestName,
        converted, error),
        "snapshot body revision must match the explicitly requested revision");
}

void TestStructuredDifferencePreview()
{
    json local;
    json remote;
    std::string error;
    Require(DnfConvertCloudMatchSnapshot(Snapshot(), 17, false, NormalizeTestName,
        local, error), "local fixture should convert");
    remote = local;
    remote["redScore"] = 4;
    remote["players"][0]["name"] = "Remote Red";
    remote["players"][0]["kills"] = 9;
    remote["lastKillerTeam"] = 1;

    const json preview = DnfBuildCloudMatchPreview(local, remote, true);
    Require(preview["swapped"] == true && preview["differenceCount"] == 4,
        "preview should expose orientation and exact difference count");
    Require(preview["groups"].size() == 4,
        "preview should always expose score, roster, stats, and state groups");
    Require(preview["groups"][0]["id"] == "score" &&
        preview["groups"][0]["items"][0]["field"] == "redScore",
        "score differences should be flat and structured");
    Require(preview["groups"][1]["items"][0]["local"] == "Red 1" &&
        preview["groups"][1]["items"][0]["remote"] == "Remote Red",
        "roster differences should preserve display names");
}

void TestRelativeOrientationUsesLocalAndTargetConsensusFlags()
{
    const json members = json::array({
        { { "deviceId", "local-device-0001" }, { "swapped", true } },
        { { "deviceId", "target-device-0002" }, { "swapped", false } }
    });
    bool relativeSwap = false;
    std::string error;
    Require(DnfResolveCloudMatchRelativeSwap(members, "local-device-0001",
        "target-device-0002", relativeSwap, error) && relativeSwap,
        "local swapped and target normal must require a relative swap");

    json bothSwapped = members;
    bothSwapped[1]["swapped"] = true;
    Require(DnfResolveCloudMatchRelativeSwap(bothSwapped, "local-device-0001",
        "target-device-0002", relativeSwap, error) && !relativeSwap,
        "two members swapped relative to consensus must be normal relative to each other");

    Require(!DnfResolveCloudMatchRelativeSwap(members, "missing-local-device",
        "target-device-0002", relativeSwap, error) && error == "local_member_missing",
        "a missing local comparison member must block orientation instead of guessing");
}

void TestPreviewCorrelationInvalidatesOnRoomRevisionAndConnectionGeneration()
{
    DnfCloudMatchPreviewBinding binding;
    binding.roomId = "59";
    binding.connectionGeneration = 7;
    binding.roomRevision = 41;
    binding.requestId = "snapshot-7-3";
    binding.deviceId = "target-device-0002";
    binding.snapshotRevision = 9;

    DnfCloudMatchCurrentState current;
    current.connected = true;
    current.roomConfirmed = true;
    current.roomId = "59";
    current.connectionGeneration = 7;
    current.roomRevision = 41;
    current.requestId = "snapshot-7-3";
    current.deviceId = "target-device-0002";
    current.snapshotRevision = 9;
    Require(DnfIsCloudMatchPreviewCurrent(binding, current),
        "all preview correlation fields should permit explicit apply");

    current.roomRevision = 42;
    Require(!DnfIsCloudMatchPreviewCurrent(binding, current),
        "room revision changes must invalidate preview");
    current.roomRevision = 41;
    current.connectionGeneration = 8;
    Require(!DnfIsCloudMatchPreviewCurrent(binding, current),
        "reconnect generation changes must invalidate preview");
    current.connectionGeneration = 7;
    current.connected = false;
    Require(!DnfIsCloudMatchPreviewCurrent(binding, current),
        "disconnection must invalidate preview");
}

void TestUndoRequiresEpochHashRoomAndConnectionGeneration()
{
    DnfCloudMatchUndoGuard guard;
    guard.available = true;
    guard.postApplyEpoch = 12;
    guard.postApplyHash = DnfCloudMatchContentHash(json({ { "score", 3 } }));
    guard.roomId = "59";
    guard.connectionGeneration = 4;

    Require(DnfCanCloudMatchUndo(guard, 12, guard.postApplyHash, "59", 4),
        "unchanged cloud apply should allow one undo");
    Require(!DnfCanCloudMatchUndo(guard, 14, guard.postApplyHash, "59", 4),
        "changed then changed back must remain invalid through the monotonic epoch");
    Require(!DnfCanCloudMatchUndo(guard, 12, guard.postApplyHash, "li-yong", 4),
        "undo must not cross rooms");
    Require(!DnfCanCloudMatchUndo(guard, 12, guard.postApplyHash, "59", 5),
        "undo must not cross connection generations");
    guard.available = false;
    Require(!DnfCanCloudMatchUndo(guard, 12, guard.postApplyHash, "59", 4),
        "consumed undo must not be available a second time");
}

} // namespace

int main()
{
    TestStrictConversionAndSwappedOrientation();
    TestStrictCloudSchemaRejections();
    TestSnapshotRevisionCorrelation();
    TestStructuredDifferencePreview();
    TestRelativeOrientationUsesLocalAndTargetConsensusFlags();
    TestPreviewCorrelationInvalidatesOnRoomRevisionAndConnectionGeneration();
    TestUndoRequiresEpochHashRoomAndConnectionGeneration();
    std::cout << "Cloud match sync tests passed.\n";
    return 0;
}
