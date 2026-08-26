#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <deque>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>

enum class KeyMappingLanRole
{
    standalone,
    server,
    client
};

struct KeyMappingLanServerInfo
{
    std::string id;
    std::string name;
    std::string address;
    unsigned short port = 18778;
};

struct KeyMappingLanStatusSnapshot
{
    KeyMappingLanRole role = KeyMappingLanRole::standalone;
    bool running = false;
    bool connected = false;
    bool reconnecting = false;
    bool discovering = false;
    bool teamSyncSupported = false;
    bool teamSyncPushSupported = false;
    bool teamSyncBidirectionalSupported = false;
    bool teamSyncSubscribed = false;
    bool teamSyncClientWriteAllowed = false;
    bool teamSyncAutoSend = false;
    std::string status;
    std::string remoteDeviceName;
    std::string serverId;
    std::string serverAddress;
    std::string pairCode;
    unsigned short port = 18778;
    std::vector<std::string> localAddresses;
    std::vector<KeyMappingLanServerInfo> discoveredServers;
};

class KeyMappingLanService
{
public:
    using StateChangedCallback = std::function<void()>;
    using TeamSyncMessageCallback = std::function<void(const std::string&)>;

    KeyMappingLanService();
    ~KeyMappingLanService();

    KeyMappingLanService(const KeyMappingLanService&) = delete;
    KeyMappingLanService& operator=(const KeyMappingLanService&) = delete;

    void SetStateChangedCallback(StateChangedCallback callback);
    void SetTeamSyncMessageCallback(TeamSyncMessageCallback callback);
    void SetTeamSyncSnapshot(const std::string& snapshot);
    void SetRemoteTeamSyncSnapshot(const std::string& snapshot, std::uint64_t revision);
    void SetTeamSyncClientWriteAllowed(bool allowed);
    void SetTeamSyncAutoSend(bool enabled);
    void CompleteTeamSyncProposal(const std::string& sourceId, unsigned int proposalId,
        bool accepted, const std::string& reason);
    void SetRole(KeyMappingLanRole role);
    bool StartServer(unsigned short port, const std::string& pairCode,
        const std::string& serverId, const std::string& deviceName, std::string& error);
    bool StartClient(const std::string& address, unsigned short port,
        const std::string& pairCode, const std::string& clientId,
        const std::string& deviceName, std::string& error);
    bool DiscoverServers(unsigned short port, std::string& error);
    void StopNetwork();
    void StopDiscovery();

    void SetLocalActiveMask(unsigned int mask);
    unsigned int GetRemoteActiveMask() const;
    bool RequestTeamSync(std::string& error);
    void SetTeamSyncSubscribed(bool enabled);
    bool IsTeamSyncPending() const;
    KeyMappingLanStatusSnapshot GetStatusSnapshot() const;

private:
    void ServerThreadMain();
    void ClientThreadMain();
    void DiscoveryThreadMain(unsigned short port);
    void NotifyStateChanged();
    void NotifyTeamSyncMessage(const std::string& message);
    void CloseTrackedSockets();
    void CloseTrackedSocket(SOCKET& member, SOCKET expected);
    bool TrackSocket(SOCKET& member, SOCKET value);

    static bool SendJsonFrame(SOCKET socket, const std::string& payload);
    static bool ReceiveJsonFrame(SOCKET socket, std::string& payload);
    static bool SetSocketNoDelay(SOCKET socket);
    static bool ConnectSocketInterruptibly(const std::string& address,
        unsigned short port, std::atomic<bool>& running, SOCKET& socketOut);
    static std::vector<std::string> GetLocalIPv4Addresses();

    WSADATA m_wsaData{};
    bool m_wsaReady = false;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_discoveryRunning{ false };
    std::atomic<unsigned int> m_localActiveMask{ 0 };
    std::atomic<unsigned int> m_remoteActiveMask{ 0 };
    std::atomic<unsigned int> m_teamSyncRequestId{ 0 };
    std::atomic<bool> m_teamSyncPending{ false };
    std::atomic<bool> m_teamSyncSubscriptionDesired{ false };
    std::atomic<unsigned int> m_teamSyncSubscriptionId{ 0 };
    std::atomic<std::uint64_t> m_teamSyncRevision{ 0 };
    std::atomic<bool> m_teamSyncClientWriteAllowed{ false };
    std::atomic<bool> m_teamSyncAutoSend{ false };
    std::atomic<unsigned int> m_teamSyncWritePolicyGeneration{ 0 };
    std::thread m_networkThread;
    std::thread m_discoveryThread;

    mutable std::mutex m_statusMutex;
    KeyMappingLanStatusSnapshot m_status;
    std::string m_deviceName;
    std::string m_localDeviceId;

    mutable std::mutex m_callbackMutex;
    StateChangedCallback m_stateChangedCallback;

    mutable std::mutex m_teamSyncCallbackMutex;
    TeamSyncMessageCallback m_teamSyncMessageCallback;

    mutable std::mutex m_teamSyncSnapshotMutex;
    std::string m_teamSyncSnapshot;

    struct TeamSyncProposalResult {
        std::string sourceId;
        unsigned int proposalId = 0;
        bool accepted = false;
        std::string reason;
    };
    std::mutex m_teamSyncProposalResultMutex;
    std::deque<TeamSyncProposalResult> m_teamSyncProposalResults;

    std::mutex m_socketMutex;
    SOCKET m_listenSocket = INVALID_SOCKET;
    SOCKET m_udpSocket = INVALID_SOCKET;
    SOCKET m_connectionSocket = INVALID_SOCKET;
    SOCKET m_discoverySocket = INVALID_SOCKET;
};
