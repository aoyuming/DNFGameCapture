#include "../KeyMappingLanService.h"
#include "../json.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using json = nlohmann::json;

static bool WaitFor(const std::function<bool()>& predicate, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

int main()
{
    KeyMappingLanService server;
    KeyMappingLanService client;
    KeyMappingLanService secondClient;
    std::string error;

    std::mutex teamSyncMutex;
    std::vector<json> teamSyncMessages;
    std::vector<json> serverTeamSyncMessages;
    json snapshot = {
        { "version", 1 },
        { "redScore", 7 },
        { "blueScore", 5 },
        { "redPickMode", "first" },
        { "isFlipped", false },
        { "outputSeatLabelToKillFile", false },
        { "lastKillerTeam", -1 },
        { "players", json::array() }
    };
    for (int i = 0; i < 8; ++i) {
        json aliases = json::array();
        for (int aliasIndex = 0; aliasIndex < 12; ++aliasIndex) {
            aliases.push_back("large-alias-" + std::to_string(i) + "-" +
                std::to_string(aliasIndex) + "-" + std::string(48, 'x'));
        }
        snapshot["players"].push_back({
            { "team", i < 4 ? 0 : 1 },
            { "name", i == 0 ? "server-player" : "" },
            { "aliases", std::move(aliases) },
            { "kills", i },
            { "deaths", 0 },
            { "akCount", 0 },
            { "currentStreak", 0 }
        });
    }
    assert(snapshot.dump().size() > 4096);
    server.SetTeamSyncSnapshot(snapshot.dump());
    client.SetTeamSyncMessageCallback([&](const std::string& message) {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        teamSyncMessages.push_back(json::parse(message));
    });
    server.SetTeamSyncMessageCallback([&](const std::string& message) {
        const json parsed = json::parse(message);
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        serverTeamSyncMessages.push_back(parsed);
    });

    assert(server.StartServer(18778, "4321", "server-test", "server", error));
    assert(client.StartClient("127.0.0.1", 18778, "4321", "client-test", "client", error));
    assert(WaitFor([&]() { return server.GetStatusSnapshot().connected; }, 2000));

    assert(client.RequestTeamSync(error));
    std::string duplicateError;
    assert(!client.RequestTeamSync(duplicateError));
    assert(WaitFor([&]() {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        return !teamSyncMessages.empty();
    }, 2000));
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        const json response = teamSyncMessages.back();
        assert(response.value("type", std::string()) == "team_sync_snapshot");
        assert(response["snapshot"].value("redScore", 0) == 7);
        assert(response["snapshot"]["players"].size() == 8);
        assert(response["snapshot"]["players"][0]["aliases"].size() == 12);
        teamSyncMessages.clear();
    }

    client.SetTeamSyncSubscribed(true);
    assert(WaitFor([&]() { return client.GetStatusSnapshot().teamSyncSubscribed; }, 1000));
    assert(WaitFor([&]() {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        return !teamSyncMessages.empty() &&
            teamSyncMessages.back().value("type", std::string()) == "team_sync_push";
    }, 2000));
    unsigned long long firstRevision = 0;
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        const json response = teamSyncMessages.back();
        firstRevision = response.value("revision", 0ull);
        assert(firstRevision > 0);
        assert(response["snapshot"].value("redScore", 0) == 7);
        teamSyncMessages.clear();
    }

    // Capability advertised as team_sync_bidirectional_v1.
    assert(client.GetStatusSnapshot().teamSyncBidirectionalSupported);
    assert(!client.GetStatusSnapshot().teamSyncClientWriteAllowed);
    client.SetTeamSyncAutoSend(true);
    snapshot["blueScore"] = 8;
    client.SetTeamSyncSnapshot(snapshot.dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        assert(serverTeamSyncMessages.empty());
    }

    // The server advertises the change through team_sync_write_policy.
    server.SetTeamSyncClientWriteAllowed(true);
    assert(WaitFor([&]() { return client.GetStatusSnapshot().teamSyncClientWriteAllowed; }, 1000));
    snapshot["blueScore"] = 9;
    client.SetTeamSyncSnapshot(snapshot.dump());
    assert(WaitFor([&]() {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        return !serverTeamSyncMessages.empty() &&
            serverTeamSyncMessages.back().value("type", std::string()) == "team_sync_propose";
    }, 2000));
    json proposal;
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        proposal = serverTeamSyncMessages.back();
    }
    assert(!proposal.value("sourceId", std::string()).empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        assert(teamSyncMessages.empty());
    }
    server.SetTeamSyncSnapshot(proposal["snapshot"].dump());
    server.CompleteTeamSyncProposal(proposal.value("sourceId", std::string()),
        proposal.value("proposalId", 0u), true, "");
    assert(WaitFor([&]() {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        return !teamSyncMessages.empty() &&
            teamSyncMessages.back().value("type", std::string()) == "team_sync_push" &&
            teamSyncMessages.back()["snapshot"].value("blueScore", 0) == 9;
    }, 2000));
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        const json canonical = teamSyncMessages.back();
        client.SetRemoteTeamSyncSnapshot(canonical["snapshot"].dump(),
            canonical.value("revision", 0ull));
        serverTeamSyncMessages.clear();
        teamSyncMessages.clear();
    }
    client.SetTeamSyncSnapshot(snapshot.dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        assert(serverTeamSyncMessages.empty());
    }

    for (int score = 8; score <= 10; ++score) {
        snapshot["redScore"] = score;
        server.SetTeamSyncSnapshot(snapshot.dump());
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
    }
    server.SetTeamSyncSnapshot(snapshot.dump());
    assert(WaitFor([&]() {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        return !teamSyncMessages.empty() &&
            teamSyncMessages.back().value("type", std::string()) == "team_sync_push" &&
            teamSyncMessages.back().value("revision", 0ull) > firstRevision;
    }, 2000));
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        assert(teamSyncMessages.size() == 1);
        assert(teamSyncMessages.back()["snapshot"].value("redScore", 0) == 10);
        teamSyncMessages.clear();
    }

    client.SetTeamSyncSubscribed(false);
    assert(WaitFor([&]() { return !client.GetStatusSnapshot().teamSyncSubscribed; }, 1000));
    snapshot["redScore"] = 11;
    server.SetTeamSyncSnapshot(snapshot.dump());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        assert(teamSyncMessages.empty());
    }

    json oversizedSnapshot = snapshot;
    oversizedSnapshot["players"][0]["aliases"] = json::array({ std::string(70000, 'z') });
    server.SetTeamSyncSnapshot(oversizedSnapshot.dump());
    assert(client.RequestTeamSync(error));
    assert(WaitFor([&]() {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        return !teamSyncMessages.empty();
    }, 2000));
    {
        std::lock_guard<std::mutex> lock(teamSyncMutex);
        const json response = teamSyncMessages.back();
        assert(response.value("type", std::string()) == "team_sync_error");
        assert(response.value("reason", std::string()) == "payload_too_large");
    }

    client.SetLocalActiveMask(0x3FFF);
    assert(WaitFor([&]() { return server.GetRemoteActiveMask() == 0x3FFF; }, 1000));

    assert(secondClient.StartClient("127.0.0.1", 18778, "4321", "client-2", "second", error));
    const bool secondRejected = WaitFor([&]() {
        const auto status = secondClient.GetStatusSnapshot();
        return status.status == "rejected_busy" || !status.running;
    }, 2500);
    if (!secondRejected) {
        const auto serverStatus = server.GetStatusSnapshot();
        const auto firstStatus = client.GetStatusSnapshot();
        const auto secondStatus = secondClient.GetStatusSnapshot();
        std::cerr << "server=" << serverStatus.status << ", connected=" << serverStatus.connected
            << "; first=" << firstStatus.status << ", connected=" << firstStatus.connected
            << "; second=" << secondStatus.status << ", connected=" << secondStatus.connected
            << ", running=" << secondStatus.running << '\n';
    }
    assert(secondRejected);

    client.StopNetwork();
    assert(WaitFor([&]() { return server.GetRemoteActiveMask() == 0; }, 1500));

    server.StopNetwork();
    secondClient.StopNetwork();
    std::cout << "key mapping LAN protocol test passed\n";
    return 0;
}
