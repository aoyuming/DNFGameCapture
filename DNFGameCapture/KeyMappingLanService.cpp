#include "pch.h"
#include "KeyMappingLanService.h"

#include <algorithm>
#include <chrono>
#include <set>
#include "json.hpp"

#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;

namespace
{
    constexpr int kProtocolVersion = 1;
    constexpr unsigned int kMaximumActiveMask = 0x3FFF;
    constexpr unsigned int kMaximumPayloadBytes = 65536;
    constexpr int kStateHeartbeatMs = 250;
    constexpr int kServerHeartbeatTimeoutMs = 1000;
    constexpr int kReconnectIntervalMs = 2000;
    constexpr int kSocketPollMs = 50;
    constexpr int kTeamSyncDebounceMs = 200;
    constexpr char kDiscoveryRequest[] = "dnf-key-mapping-discover";

    using Clock = std::chrono::steady_clock;

    bool SendAll(SOCKET socket, const char* data, int size)
    {
        int sent = 0;
        while (sent < size) {
            const int count = ::send(socket, data + sent, size - sent, 0);
            if (count <= 0) return false;
            sent += count;
        }
        return true;
    }

    bool ReceiveAll(SOCKET socket, char* data, int size)
    {
        int received = 0;
        while (received < size) {
            const int count = ::recv(socket, data + received, size - received, 0);
            if (count <= 0) return false;
            received += count;
        }
        return true;
    }

    void SetSocketTimeouts(SOCKET socket, DWORD timeoutMs)
    {
        ::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    }

    std::string SockaddrToString(const sockaddr_in& address)
    {
        char text[INET_ADDRSTRLEN] = {};
        if (!::inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text))) return {};
        return text;
    }
}

KeyMappingLanService::KeyMappingLanService()
{
    m_wsaReady = ::WSAStartup(MAKEWORD(2, 2), &m_wsaData) == 0;
}

KeyMappingLanService::~KeyMappingLanService()
{
    SetStateChangedCallback(nullptr);
    SetTeamSyncMessageCallback(nullptr);
    StopNetwork();
    StopDiscovery();
    if (m_wsaReady) ::WSACleanup();
}

void KeyMappingLanService::SetStateChangedCallback(StateChangedCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_stateChangedCallback = std::move(callback);
}

void KeyMappingLanService::SetTeamSyncMessageCallback(TeamSyncMessageCallback callback)
{
    std::lock_guard<std::mutex> lock(m_teamSyncCallbackMutex);
    m_teamSyncMessageCallback = std::move(callback);
}

void KeyMappingLanService::SetTeamSyncSnapshot(const std::string& snapshot)
{
    std::lock_guard<std::mutex> lock(m_teamSyncSnapshotMutex);
    if (m_teamSyncSnapshot == snapshot) return;
    m_teamSyncSnapshot = snapshot;
    m_teamSyncRevision.fetch_add(1, std::memory_order_acq_rel);
}

void KeyMappingLanService::SetRemoteTeamSyncSnapshot(const std::string& snapshot,
    std::uint64_t revision)
{
    {
        std::lock_guard<std::mutex> lock(m_teamSyncSnapshotMutex);
        m_teamSyncSnapshot = snapshot;
    }
    // The server owns canonical revisions. Recording a remote snapshot must not
    // increment the client's local revision or it would be uploaded again.
    (void)revision;
}

void KeyMappingLanService::SetTeamSyncClientWriteAllowed(bool allowed)
{
    if (m_teamSyncClientWriteAllowed.exchange(allowed, std::memory_order_acq_rel) != allowed) {
        m_teamSyncWritePolicyGeneration.fetch_add(1, std::memory_order_acq_rel);
    }
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.teamSyncClientWriteAllowed = allowed;
    }
    NotifyStateChanged();
}

void KeyMappingLanService::SetTeamSyncAutoSend(bool enabled)
{
    m_teamSyncAutoSend.store(enabled, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.teamSyncAutoSend = enabled;
    }
    NotifyStateChanged();
}

void KeyMappingLanService::CompleteTeamSyncProposal(const std::string& sourceId,
    unsigned int proposalId, bool accepted, const std::string& reason)
{
    if (sourceId.empty() || proposalId == 0) return;
    std::lock_guard<std::mutex> lock(m_teamSyncProposalResultMutex);
    m_teamSyncProposalResults.push_back({ sourceId, proposalId, accepted, reason });
}

void KeyMappingLanService::SetRole(KeyMappingLanRole role)
{
    StopNetwork();
    StopDiscovery();
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.role = role;
        m_status.status.clear();
        m_status.teamSyncPushSupported = role == KeyMappingLanRole::server;
        m_status.teamSyncBidirectionalSupported = role == KeyMappingLanRole::server;
        m_status.teamSyncSubscribed = false;
        m_status.teamSyncClientWriteAllowed = role == KeyMappingLanRole::server &&
            m_teamSyncClientWriteAllowed.load(std::memory_order_acquire);
    }
    NotifyStateChanged();
}

bool KeyMappingLanService::StartServer(unsigned short port, const std::string& pairCode,
    const std::string& serverId, const std::string& deviceName, std::string& error)
{
    error.clear();
    if (!m_wsaReady) {
        error = "Winsock initialization failed";
        return false;
    }
    if (port == 0 || pairCode.size() != 4) {
        error = "Invalid server port or pair code";
        return false;
    }

    StopNetwork();
    m_deviceName = deviceName;
    m_localDeviceId = serverId;
    m_remoteActiveMask.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.role = KeyMappingLanRole::server;
        m_status.running = true;
        m_status.connected = false;
        m_status.reconnecting = false;
        m_status.status = "starting";
        m_status.teamSyncSupported = true;
        m_status.teamSyncPushSupported = true;
        m_status.teamSyncBidirectionalSupported = true;
        m_status.teamSyncSubscribed = false;
        m_status.teamSyncClientWriteAllowed =
            m_teamSyncClientWriteAllowed.load(std::memory_order_acquire);
        m_status.remoteDeviceName.clear();
        m_status.serverId = serverId;
        m_status.serverAddress.clear();
        m_status.pairCode = pairCode;
        m_status.port = port;
        m_status.localAddresses = GetLocalIPv4Addresses();
    }
    m_running.store(true, std::memory_order_release);
    try {
        m_networkThread = std::thread(&KeyMappingLanService::ServerThreadMain, this);
    }
    catch (...) {
        m_running.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.running = false;
        m_status.status = "thread_start_failed";
        error = "Unable to start server thread";
        return false;
    }
    NotifyStateChanged();
    return true;
}

bool KeyMappingLanService::StartClient(const std::string& address, unsigned short port,
    const std::string& pairCode, const std::string& clientId,
    const std::string& deviceName, std::string& error)
{
    error.clear();
    if (!m_wsaReady) {
        error = "Winsock initialization failed";
        return false;
    }
    if (address.empty() || port == 0 || pairCode.size() != 4) {
        error = "Invalid server address, port, or pair code";
        return false;
    }

    StopNetwork();
    m_deviceName = deviceName;
    m_localDeviceId = clientId;
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.role = KeyMappingLanRole::client;
        m_status.running = true;
        m_status.connected = false;
        m_status.reconnecting = false;
        m_status.status = "connecting";
        m_status.teamSyncSupported = false;
        m_status.teamSyncPushSupported = false;
        m_status.teamSyncBidirectionalSupported = false;
        m_status.teamSyncSubscribed = false;
        m_status.teamSyncClientWriteAllowed = false;
        m_status.teamSyncAutoSend = m_teamSyncAutoSend.load(std::memory_order_acquire);
        m_status.remoteDeviceName.clear();
        m_status.serverAddress = address;
        m_status.pairCode = pairCode;
        m_status.port = port;
    }
    m_running.store(true, std::memory_order_release);
    try {
        m_networkThread = std::thread(&KeyMappingLanService::ClientThreadMain, this);
    }
    catch (...) {
        m_running.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.running = false;
        m_status.status = "thread_start_failed";
        error = "Unable to start client thread";
        return false;
    }
    NotifyStateChanged();
    return true;
}

bool KeyMappingLanService::DiscoverServers(unsigned short port, std::string& error)
{
    error.clear();
    if (!m_wsaReady || port == 0) {
        error = "Winsock is unavailable or port is invalid";
        return false;
    }

    StopDiscovery();
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.discovering = true;
        m_status.discoveredServers.clear();
    }
    m_discoveryRunning.store(true, std::memory_order_release);
    try {
        m_discoveryThread = std::thread(&KeyMappingLanService::DiscoveryThreadMain, this, port);
    }
    catch (...) {
        m_discoveryRunning.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.discovering = false;
        error = "Unable to start discovery thread";
        return false;
    }
    NotifyStateChanged();
    return true;
}

void KeyMappingLanService::StopNetwork()
{
    m_running.store(false, std::memory_order_release);
    CloseTrackedSockets();
    if (m_networkThread.joinable()) m_networkThread.join();
    m_remoteActiveMask.store(0, std::memory_order_release);
    m_teamSyncPending.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_teamSyncProposalResultMutex);
        m_teamSyncProposalResults.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.running = false;
        m_status.connected = false;
        m_status.reconnecting = false;
        m_status.teamSyncPushSupported = false;
        m_status.teamSyncBidirectionalSupported = false;
        m_status.teamSyncSubscribed = false;
        m_status.teamSyncClientWriteAllowed = false;
        m_status.remoteDeviceName.clear();
        if (!m_status.status.empty()) m_status.status = "stopped";
    }
    NotifyStateChanged();
}

void KeyMappingLanService::StopDiscovery()
{
    m_discoveryRunning.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_socketMutex);
        if (m_discoverySocket != INVALID_SOCKET) {
            ::closesocket(m_discoverySocket);
            m_discoverySocket = INVALID_SOCKET;
        }
    }
    if (m_discoveryThread.joinable()) m_discoveryThread.join();
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.discovering = false;
    }
}

void KeyMappingLanService::SetLocalActiveMask(unsigned int mask)
{
    m_localActiveMask.store(mask & kMaximumActiveMask, std::memory_order_release);
}

unsigned int KeyMappingLanService::GetRemoteActiveMask() const
{
    return m_remoteActiveMask.load(std::memory_order_acquire) & kMaximumActiveMask;
}

bool KeyMappingLanService::RequestTeamSync(std::string& error)
{
    error.clear();
    const KeyMappingLanStatusSnapshot status = GetStatusSnapshot();
    if (status.role != KeyMappingLanRole::client) {
        error = "team sync requires client role";
        return false;
    }
    if (!status.running || !status.connected || !m_running.load(std::memory_order_acquire)) {
        error = "client is not connected";
        return false;
    }
    if (!status.teamSyncSupported) {
        error = "server does not support team sync";
        return false;
    }
    bool expected = false;
    if (!m_teamSyncPending.compare_exchange_strong(expected, true,
        std::memory_order_acq_rel)) {
        error = "team sync is already pending";
        return false;
    }
    unsigned int nextId = m_teamSyncRequestId.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (nextId == 0) {
        m_teamSyncRequestId.store(1, std::memory_order_release);
    }
    return true;
}

void KeyMappingLanService::SetTeamSyncSubscribed(bool enabled)
{
    const bool previous = m_teamSyncSubscriptionDesired.exchange(enabled,
        std::memory_order_acq_rel);
    if (previous != enabled) {
        unsigned int nextId = m_teamSyncSubscriptionId.fetch_add(1,
            std::memory_order_acq_rel) + 1;
        if (nextId == 0) {
            m_teamSyncSubscriptionId.store(1, std::memory_order_release);
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.teamSyncSubscribed = enabled &&
            m_status.role == KeyMappingLanRole::client &&
            m_status.connected && m_status.teamSyncPushSupported;
    }
    NotifyStateChanged();
}

bool KeyMappingLanService::IsTeamSyncPending() const
{
    return m_teamSyncPending.load(std::memory_order_acquire);
}

KeyMappingLanStatusSnapshot KeyMappingLanService::GetStatusSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_status;
}

void KeyMappingLanService::NotifyStateChanged()
{
    StateChangedCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_stateChangedCallback;
    }
    if (callback) callback();
}

void KeyMappingLanService::NotifyTeamSyncMessage(const std::string& message)
{
    TeamSyncMessageCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_teamSyncCallbackMutex);
        callback = m_teamSyncMessageCallback;
    }
    if (callback) callback(message);
}

bool KeyMappingLanService::TrackSocket(SOCKET& member, SOCKET value)
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    if ((!m_running.load(std::memory_order_acquire) && &member != &m_discoverySocket) ||
        (!m_discoveryRunning.load(std::memory_order_acquire) && &member == &m_discoverySocket)) {
        ::closesocket(value);
        return false;
    }
    member = value;
    return true;
}

void KeyMappingLanService::CloseTrackedSocket(SOCKET& member, SOCKET expected)
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    if (member == expected && member != INVALID_SOCKET) {
        ::shutdown(member, SD_BOTH);
        ::closesocket(member);
        member = INVALID_SOCKET;
    }
}

void KeyMappingLanService::CloseTrackedSockets()
{
    std::lock_guard<std::mutex> lock(m_socketMutex);
    SOCKET* sockets[] = { &m_listenSocket, &m_udpSocket, &m_connectionSocket };
    for (SOCKET* socket : sockets) {
        if (*socket == INVALID_SOCKET) continue;
        ::shutdown(*socket, SD_BOTH);
        ::closesocket(*socket);
        *socket = INVALID_SOCKET;
    }
}

bool KeyMappingLanService::SendJsonFrame(SOCKET socket, const std::string& payload)
{
    if (payload.empty() || payload.size() > kMaximumPayloadBytes) return false;
    const unsigned long length = ::htonl(static_cast<unsigned long>(payload.size()));
    return SendAll(socket, reinterpret_cast<const char*>(&length), sizeof(length)) &&
        SendAll(socket, payload.data(), static_cast<int>(payload.size()));
}

bool KeyMappingLanService::ReceiveJsonFrame(SOCKET socket, std::string& payload)
{
    payload.clear();
    unsigned long networkLength = 0;
    if (!ReceiveAll(socket, reinterpret_cast<char*>(&networkLength), sizeof(networkLength))) return false;
    const unsigned long length = ::ntohl(networkLength);
    if (length == 0 || length > kMaximumPayloadBytes) return false;
    payload.resize(length);
    return ReceiveAll(socket, payload.data(), static_cast<int>(length));
}

bool KeyMappingLanService::SetSocketNoDelay(SOCKET socket)
{
    const BOOL enabled = TRUE;
    return ::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
}

bool KeyMappingLanService::ConnectSocketInterruptibly(const std::string& address,
    unsigned short port, std::atomic<bool>& running, SOCKET& socketOut)
{
    socketOut = INVALID_SOCKET;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    if (::getaddrinfo(address.c_str(), portText.c_str(), &hints, &results) != 0) return false;

    bool connected = false;
    for (addrinfo* item = results; item && running.load(std::memory_order_acquire); item = item->ai_next) {
        SOCKET candidate = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (candidate == INVALID_SOCKET) continue;
        u_long nonBlocking = 1;
        ::ioctlsocket(candidate, FIONBIO, &nonBlocking);
        const int result = ::connect(candidate, item->ai_addr, static_cast<int>(item->ai_addrlen));
        if (result == 0 || ::WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(candidate, &writeSet);
            timeval timeout{ 0, 200000 };
            int attempts = 0;
            while (running.load(std::memory_order_acquire) && attempts++ < 10) {
                fd_set currentSet = writeSet;
                const int ready = ::select(0, nullptr, &currentSet, nullptr, &timeout);
                if (ready > 0) {
                    int socketError = 0;
                    int errorLength = sizeof(socketError);
                    ::getsockopt(candidate, SOL_SOCKET, SO_ERROR,
                        reinterpret_cast<char*>(&socketError), &errorLength);
                    connected = socketError == 0;
                    break;
                }
                if (ready == SOCKET_ERROR) break;
            }
        }
        nonBlocking = 0;
        ::ioctlsocket(candidate, FIONBIO, &nonBlocking);
        if (connected) {
            socketOut = candidate;
            break;
        }
        ::closesocket(candidate);
    }
    ::freeaddrinfo(results);
    return connected;
}

std::vector<std::string> KeyMappingLanService::GetLocalIPv4Addresses()
{
    std::set<std::string> unique;
    char hostName[256] = {};
    if (::gethostname(hostName, sizeof(hostName)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* results = nullptr;
        if (::getaddrinfo(hostName, nullptr, &hints, &results) == 0) {
            for (addrinfo* item = results; item; item = item->ai_next) {
                const auto* address = reinterpret_cast<const sockaddr_in*>(item->ai_addr);
                const std::string text = SockaddrToString(*address);
                if (!text.empty() && text != "127.0.0.1") unique.insert(text);
            }
            ::freeaddrinfo(results);
        }
    }
    if (unique.empty()) unique.insert("127.0.0.1");
    return { unique.begin(), unique.end() };
}

void KeyMappingLanService::ServerThreadMain()
{
    KeyMappingLanStatusSnapshot startup = GetStatusSnapshot();
    SOCKET listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    SOCKET udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (listenSocket == INVALID_SOCKET || udpSocket == INVALID_SOCKET) {
        if (listenSocket != INVALID_SOCKET) ::closesocket(listenSocket);
        if (udpSocket != INVALID_SOCKET) ::closesocket(udpSocket);
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status.running = false;
            m_status.status = "socket_failed";
        }
        m_running.store(false, std::memory_order_release);
        NotifyStateChanged();
        return;
    }

    const BOOL reuse = TRUE;
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    ::setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in bindAddress{};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_addr.s_addr = ::htonl(INADDR_ANY);
    bindAddress.sin_port = ::htons(startup.port);
    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress)) == SOCKET_ERROR ||
        ::listen(listenSocket, 2) == SOCKET_ERROR ||
        ::bind(udpSocket, reinterpret_cast<sockaddr*>(&bindAddress), sizeof(bindAddress)) == SOCKET_ERROR) {
        ::closesocket(listenSocket);
        ::closesocket(udpSocket);
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status.running = false;
            m_status.status = "bind_failed";
        }
        m_running.store(false, std::memory_order_release);
        NotifyStateChanged();
        return;
    }
    if (!TrackSocket(m_listenSocket, listenSocket) || !TrackSocket(m_udpSocket, udpSocket)) return;

    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.status = "listening";
    }
    NotifyStateChanged();

    SOCKET clientSocket = INVALID_SOCKET;
    bool paired = false;
    std::string activeClientId;
    bool teamSyncSubscribed = false;
    bool pushImmediately = false;
    unsigned int teamSyncSubscriptionId = 0;
    std::uint64_t lastPushedRevision = 0;
    std::uint64_t pendingPushRevision = m_teamSyncRevision.load(std::memory_order_acquire);
    unsigned int sentWritePolicyGeneration = 0;
    auto acceptedAt = Clock::now();
    auto lastHeartbeat = Clock::now();
    auto lastPing = Clock::now();
    auto pendingPushChangedAt = Clock::now();

    const auto sendTeamSyncSnapshot = [&](SOCKET target, bool automatic,
        unsigned int requestId, std::uint64_t revision,
        unsigned int subscriptionId) {
        std::string snapshotPayload;
        {
            std::lock_guard<std::mutex> lock(m_teamSyncSnapshotMutex);
            snapshotPayload = m_teamSyncSnapshot;
        }

        json reply;
        std::string reason;
        if (snapshotPayload.empty()) {
            reason = "unavailable";
        }
        else {
            try {
                const json snapshot = json::parse(snapshotPayload);
                if (!snapshot.is_object() || snapshot.value("version", 0) != kProtocolVersion) {
                    reason = "invalid_snapshot";
                }
                else if (automatic) {
                    reply = {
                        { "type", "team_sync_push" },
                        { "version", kProtocolVersion },
                        { "revision", revision },
                        { "subscriptionId", subscriptionId },
                        { "snapshot", snapshot }
                    };
                }
                else {
                    reply = {
                        { "type", "team_sync_snapshot" },
                        { "version", kProtocolVersion },
                        { "requestId", requestId },
                        { "snapshot", snapshot }
                    };
                }
            }
            catch (...) {
                reason = "snapshot_failed";
            }
        }

        if (!reason.empty()) {
            reply = {
                { "type", "team_sync_error" },
                { "version", kProtocolVersion },
                { "requestId", requestId },
                { "automatic", automatic },
                { "revision", revision },
                { "subscriptionId", subscriptionId },
                { "reason", reason }
            };
        }

        std::string payload = reply.dump();
        if (payload.size() > kMaximumPayloadBytes) {
            payload = json({
                { "type", "team_sync_error" },
                { "version", kProtocolVersion },
                { "requestId", requestId },
                { "automatic", automatic },
                { "revision", revision },
                { "subscriptionId", subscriptionId },
                { "reason", "payload_too_large" }
            }).dump();
        }
        return SendJsonFrame(target, payload);
    };

    while (m_running.load(std::memory_order_acquire)) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);
        FD_SET(udpSocket, &readSet);
        if (clientSocket != INVALID_SOCKET) FD_SET(clientSocket, &readSet);
        timeval timeout{ 0, kSocketPollMs * 1000 };
        const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            if (!m_running.load(std::memory_order_acquire)) break;
            continue;
        }

        if (FD_ISSET(udpSocket, &readSet)) {
            sockaddr_in sender{};
            int senderLength = sizeof(sender);
            char buffer[1024] = {};
            const int count = ::recvfrom(udpSocket, buffer, sizeof(buffer) - 1, 0,
                reinterpret_cast<sockaddr*>(&sender), &senderLength);
            if (count > 0) {
                try {
                    const json request = json::parse(std::string(buffer, count));
                    if (request.value("type", std::string()) == kDiscoveryRequest &&
                        request.value("version", 0) == kProtocolVersion) {
                        json response = {
                            { "type", "discovery" },
                            { "version", kProtocolVersion },
                            { "serverId", startup.serverId },
                            { "name", m_deviceName },
                            { "port", startup.port }
                        };
                        const std::string payload = response.dump();
                        ::sendto(udpSocket, payload.data(), static_cast<int>(payload.size()), 0,
                            reinterpret_cast<const sockaddr*>(&sender), senderLength);
                    }
                }
                catch (...) {}
            }
        }

        if (FD_ISSET(listenSocket, &readSet)) {
            SOCKET incoming = ::accept(listenSocket, nullptr, nullptr);
            if (incoming != INVALID_SOCKET) {
                SetSocketTimeouts(incoming, 1200);
                SetSocketNoDelay(incoming);
                if (clientSocket != INVALID_SOCKET) {
                    // Read the pending hello before closing. Closing a Windows
                    // socket with unread inbound data can emit RST and discard
                    // the queued rejection frame on the client side.
                    SetSocketTimeouts(incoming, 250);
                    std::string ignoredHello;
                    ReceiveJsonFrame(incoming, ignoredHello);
                    SendJsonFrame(incoming, json({
                        { "type", "rejected" }, { "reason", "busy" }
                    }).dump());
                    ::shutdown(incoming, SD_SEND);
                    ::closesocket(incoming);
                }
                else {
                    clientSocket = incoming;
                    paired = false;
                    activeClientId.clear();
                    teamSyncSubscribed = false;
                    teamSyncSubscriptionId = 0;
                    lastPushedRevision = 0;
                    pushImmediately = false;
                    acceptedAt = Clock::now();
                    TrackSocket(m_connectionSocket, clientSocket);
                }
            }
        }

        if (clientSocket != INVALID_SOCKET && FD_ISSET(clientSocket, &readSet)) {
            std::string payload;
            if (!ReceiveJsonFrame(clientSocket, payload)) {
                CloseTrackedSocket(m_connectionSocket, clientSocket);
                clientSocket = INVALID_SOCKET;
                paired = false;
                activeClientId.clear();
                teamSyncSubscribed = false;
                teamSyncSubscriptionId = 0;
                m_remoteActiveMask.store(0, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lock(m_statusMutex);
                    m_status.connected = false;
                    m_status.teamSyncSubscribed = false;
                    m_status.remoteDeviceName.clear();
                    m_status.status = "listening";
                }
                NotifyStateChanged();
            }
            else {
                try {
                    const json message = json::parse(payload);
                    const std::string type = message.value("type", std::string());
                    if (!paired && type == "hello") {
                        const bool validVersion = message.value("version", 0) == kProtocolVersion;
                        const bool validCode = message.value("pairCode", std::string()) == startup.pairCode;
                        if (!validVersion || !validCode) {
                            SendJsonFrame(clientSocket, json({
                                { "type", "rejected" },
                                { "reason", validVersion ? "pair_code" : "version" }
                            }).dump());
                            CloseTrackedSocket(m_connectionSocket, clientSocket);
                            clientSocket = INVALID_SOCKET;
                        }
                        else {
                            paired = true;
                            activeClientId = message.value("clientId", std::string());
                            lastHeartbeat = Clock::now();
                            SendJsonFrame(clientSocket, json({
                                { "type", "accepted" },
                                { "version", kProtocolVersion },
                                { "serverId", startup.serverId },
                                { "name", m_deviceName },
                                { "capabilities", json::array({ "team_sync_v1", "team_sync_push_v1",
                                    "team_sync_bidirectional_v1" }) }
                            }).dump());
                            const bool writeAllowed =
                                m_teamSyncClientWriteAllowed.load(std::memory_order_acquire);
                            SendJsonFrame(clientSocket, json({
                                { "type", "team_sync_write_policy" },
                                { "version", kProtocolVersion },
                                { "allowed", writeAllowed }
                            }).dump());
                            sentWritePolicyGeneration =
                                m_teamSyncWritePolicyGeneration.load(std::memory_order_acquire);
                            {
                                std::lock_guard<std::mutex> lock(m_statusMutex);
                                m_status.connected = true;
                                m_status.teamSyncSubscribed = false;
                                m_status.remoteDeviceName = message.value("name", std::string());
                                m_status.status = "connected";
                            }
                            NotifyStateChanged();
                        }
                    }
                    else if (paired && type == "state") {
                        const unsigned int mask = message.value("activeMask", 0u);
                        if (mask <= kMaximumActiveMask) {
                            m_remoteActiveMask.store(mask, std::memory_order_release);
                            lastHeartbeat = Clock::now();
                        }
                        else {
                            CloseTrackedSocket(m_connectionSocket, clientSocket);
                            clientSocket = INVALID_SOCKET;
                            paired = false;
                            activeClientId.clear();
                            teamSyncSubscribed = false;
                            teamSyncSubscriptionId = 0;
                            m_remoteActiveMask.store(0, std::memory_order_release);
                            {
                                std::lock_guard<std::mutex> lock(m_statusMutex);
                                m_status.connected = false;
                                m_status.teamSyncSubscribed = false;
                                m_status.remoteDeviceName.clear();
                                m_status.status = "listening";
                            }
                            NotifyStateChanged();
                        }
                    }
                    else if (paired && type == "team_sync_request") {
                        const unsigned int requestId = message.value("requestId", 0u);
                        if (requestId == 0) {
                            if (!SendJsonFrame(clientSocket, json({
                                { "type", "team_sync_error" },
                                { "version", kProtocolVersion },
                                { "requestId", requestId },
                                { "reason", "invalid_request" }
                            }).dump())) {
                                throw std::runtime_error("failed to send team sync response");
                            }
                        }
                        else if (!sendTeamSyncSnapshot(clientSocket, false, requestId,
                            m_teamSyncRevision.load(std::memory_order_acquire), 0)) {
                            throw std::runtime_error("failed to send team sync response");
                        }
                    }
                    else if (paired && type == "team_sync_subscribe") {
                        const bool enabled = message.value("enabled", false);
                        const unsigned int subscriptionId = message.value("subscriptionId", 0u);
                        if (message.value("version", 0) != kProtocolVersion || subscriptionId == 0) {
                            throw std::runtime_error("invalid team sync subscription");
                        }
                        teamSyncSubscribed = enabled;
                        teamSyncSubscriptionId = subscriptionId;
                        lastPushedRevision = 0;
                        pendingPushRevision = m_teamSyncRevision.load(std::memory_order_acquire);
                        pendingPushChangedAt = Clock::now();
                        pushImmediately = enabled;
                        {
                            std::lock_guard<std::mutex> lock(m_statusMutex);
                            m_status.teamSyncSubscribed = enabled;
                        }
                        NotifyStateChanged();
                    }
                    else if (paired && type == "team_sync_propose") {
                        const unsigned int proposalId = message.value("proposalId", 0u);
                        const bool writeAllowed =
                            m_teamSyncClientWriteAllowed.load(std::memory_order_acquire);
                        const bool valid = message.value("version", 0) == kProtocolVersion &&
                            proposalId != 0 && message.contains("snapshot") &&
                            message["snapshot"].is_object() && payload.size() <= kMaximumPayloadBytes;
                        if (!writeAllowed || !valid) {
                            SendJsonFrame(clientSocket, json({
                                { "type", "team_sync_propose_result" },
                                { "version", kProtocolVersion },
                                { "proposalId", proposalId },
                                { "accepted", false },
                                { "reason", writeAllowed ? "invalid_snapshot" : "write_not_allowed" }
                            }).dump());
                        }
                        else {
                            json proposal = message;
                            proposal["sourceId"] = activeClientId;
                            NotifyTeamSyncMessage(proposal.dump());
                        }
                    }
                    else if (paired && type == "ping") {
                        lastHeartbeat = Clock::now();
                        SendJsonFrame(clientSocket, json({ { "type", "pong" } }).dump());
                    }
                    else if (paired && type == "pong") {
                        lastHeartbeat = Clock::now();
                    }
                }
                catch (...) {
                    CloseTrackedSocket(m_connectionSocket, clientSocket);
                    clientSocket = INVALID_SOCKET;
                    paired = false;
                    activeClientId.clear();
                    teamSyncSubscribed = false;
                    teamSyncSubscriptionId = 0;
                    m_remoteActiveMask.store(0, std::memory_order_release);
                    {
                        std::lock_guard<std::mutex> lock(m_statusMutex);
                        m_status.connected = false;
                        m_status.teamSyncSubscribed = false;
                        m_status.remoteDeviceName.clear();
                        m_status.status = "listening";
                    }
                    NotifyStateChanged();
                }
            }
        }

        const auto now = Clock::now();
        if (clientSocket != INVALID_SOCKET && !paired &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - acceptedAt).count() > kReconnectIntervalMs) {
            CloseTrackedSocket(m_connectionSocket, clientSocket);
            clientSocket = INVALID_SOCKET;
        }
        if (clientSocket != INVALID_SOCKET && paired) {
            std::deque<TeamSyncProposalResult> proposalResults;
            {
                std::lock_guard<std::mutex> lock(m_teamSyncProposalResultMutex);
                proposalResults.swap(m_teamSyncProposalResults);
            }
            for (const auto& result : proposalResults) {
                if (result.sourceId != activeClientId) continue;
                if (!SendJsonFrame(clientSocket, json({
                    { "type", "team_sync_propose_result" },
                    { "version", kProtocolVersion },
                    { "proposalId", result.proposalId },
                    { "accepted", result.accepted },
                    { "reason", result.reason }
                }).dump())) {
                    CloseTrackedSocket(m_connectionSocket, clientSocket);
                    clientSocket = INVALID_SOCKET;
                    paired = false;
                    activeClientId.clear();
                    break;
                }
            }
            if (clientSocket == INVALID_SOCKET) continue;
            const unsigned int writePolicyGeneration =
                m_teamSyncWritePolicyGeneration.load(std::memory_order_acquire);
            if (writePolicyGeneration != sentWritePolicyGeneration) {
                if (!SendJsonFrame(clientSocket, json({
                    { "type", "team_sync_write_policy" },
                    { "version", kProtocolVersion },
                    { "allowed", m_teamSyncClientWriteAllowed.load(std::memory_order_acquire) }
                }).dump())) {
                    CloseTrackedSocket(m_connectionSocket, clientSocket);
                    clientSocket = INVALID_SOCKET;
                    paired = false;
                    continue;
                }
                sentWritePolicyGeneration = writePolicyGeneration;
            }
            const std::uint64_t currentRevision = m_teamSyncRevision.load(std::memory_order_acquire);
            if (teamSyncSubscribed && currentRevision != pendingPushRevision) {
                pendingPushRevision = currentRevision;
                pendingPushChangedAt = now;
            }
            if (teamSyncSubscribed && pendingPushRevision != lastPushedRevision &&
                (pushImmediately || std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - pendingPushChangedAt).count() >= kTeamSyncDebounceMs)) {
                if (!sendTeamSyncSnapshot(clientSocket, true, 0, pendingPushRevision,
                    teamSyncSubscriptionId)) {
                    CloseTrackedSocket(m_connectionSocket, clientSocket);
                    clientSocket = INVALID_SOCKET;
                    paired = false;
                    teamSyncSubscribed = false;
                    teamSyncSubscriptionId = 0;
                    m_remoteActiveMask.store(0, std::memory_order_release);
                    {
                        std::lock_guard<std::mutex> lock(m_statusMutex);
                        m_status.connected = false;
                        m_status.teamSyncSubscribed = false;
                        m_status.remoteDeviceName.clear();
                        m_status.status = "listening";
                    }
                    NotifyStateChanged();
                    continue;
                }
                lastPushedRevision = pendingPushRevision;
                pushImmediately = false;
            }
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat).count() > kServerHeartbeatTimeoutMs) {
                CloseTrackedSocket(m_connectionSocket, clientSocket);
                clientSocket = INVALID_SOCKET;
                paired = false;
                teamSyncSubscribed = false;
                teamSyncSubscriptionId = 0;
                m_remoteActiveMask.store(0, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lock(m_statusMutex);
                    m_status.connected = false;
                    m_status.teamSyncSubscribed = false;
                    m_status.remoteDeviceName.clear();
                    m_status.status = "listening";
                }
                NotifyStateChanged();
            }
            else if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPing).count() >= kStateHeartbeatMs) {
                if (!SendJsonFrame(clientSocket, json({ { "type", "ping" } }).dump())) {
                    CloseTrackedSocket(m_connectionSocket, clientSocket);
                    clientSocket = INVALID_SOCKET;
                    paired = false;
                }
                lastPing = now;
            }
        }
    }

    if (clientSocket != INVALID_SOCKET) CloseTrackedSocket(m_connectionSocket, clientSocket);
    CloseTrackedSocket(m_udpSocket, udpSocket);
    CloseTrackedSocket(m_listenSocket, listenSocket);
    m_remoteActiveMask.store(0, std::memory_order_release);
}

void KeyMappingLanService::ClientThreadMain()
{
    const KeyMappingLanStatusSnapshot startup = GetStatusSnapshot();
    while (m_running.load(std::memory_order_acquire)) {
        SOCKET socket = INVALID_SOCKET;
        if (!ConnectSocketInterruptibly(startup.serverAddress, startup.port, m_running, socket)) {
            {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_status.connected = false;
                m_status.reconnecting = true;
                m_status.status = "reconnecting";
            }
            NotifyStateChanged();
        }
        else if (TrackSocket(m_connectionSocket, socket)) {
            SetSocketNoDelay(socket);
            SetSocketTimeouts(socket, 1500);
            json hello = {
                { "type", "hello" },
                { "version", kProtocolVersion },
                { "pairCode", startup.pairCode },
                { "clientId", m_localDeviceId },
                { "name", m_deviceName }
            };
            std::string responsePayload;
            bool accepted = SendJsonFrame(socket, hello.dump()) && ReceiveJsonFrame(socket, responsePayload);
            std::string rejectionReason;
            if (accepted) {
                try {
                    const json response = json::parse(responsePayload);
                    accepted = response.value("type", std::string()) == "accepted" &&
                        response.value("version", 0) == kProtocolVersion;
                    if (accepted) {
                        std::lock_guard<std::mutex> lock(m_statusMutex);
                        m_status.connected = true;
                        m_status.reconnecting = false;
                        m_status.status = "connected";
                        m_status.serverId = response.value("serverId", std::string());
                        m_status.remoteDeviceName = response.value("name", std::string());
                        m_status.teamSyncSupported = false;
                        m_status.teamSyncPushSupported = false;
                        m_status.teamSyncBidirectionalSupported = false;
                        m_status.teamSyncSubscribed = false;
                        m_status.teamSyncClientWriteAllowed = false;
                        m_status.teamSyncAutoSend = m_teamSyncAutoSend.load(std::memory_order_acquire);
                        if (response.contains("capabilities") && response["capabilities"].is_array()) {
                            for (const auto& capability : response["capabilities"]) {
                                if (!capability.is_string()) continue;
                                const std::string name = capability.get<std::string>();
                                if (name == "team_sync_v1") m_status.teamSyncSupported = true;
                                if (name == "team_sync_push_v1") m_status.teamSyncPushSupported = true;
                                if (name == "team_sync_bidirectional_v1") {
                                    m_status.teamSyncBidirectionalSupported = true;
                                }
                            }
                        }
                    }
                    else if (response.value("type", std::string()) == "rejected") {
                        rejectionReason = response.value("reason", std::string("rejected"));
                    }
                }
                catch (...) {
                    accepted = false;
                }
            }
            if (!rejectionReason.empty()) {
                {
                    std::lock_guard<std::mutex> lock(m_statusMutex);
                    m_status.connected = false;
                    m_status.reconnecting = false;
                    m_status.running = false;
                    m_status.status = "rejected_" + rejectionReason;
                }
                m_running.store(false, std::memory_order_release);
                CloseTrackedSocket(m_connectionSocket, socket);
                NotifyStateChanged();
                break;
            }
            if (accepted) {
                NotifyStateChanged();
                unsigned int lastSentMask = kMaximumActiveMask + 1;
                auto lastSentAt = Clock::now() - std::chrono::milliseconds(kStateHeartbeatMs);
                unsigned int sentTeamSyncRequestId = 0;
                auto teamSyncRequestedAt = Clock::now();
                unsigned int sentSubscriptionId = 0;
                bool sentSubscriptionEnabled = false;
                std::uint64_t lastReceivedPushRevision = 0;
                std::uint64_t lastObservedLocalRevision =
                    m_teamSyncRevision.load(std::memory_order_acquire);
                std::uint64_t pendingUploadRevision = lastObservedLocalRevision;
                std::uint64_t lastUploadedRevision = lastObservedLocalRevision;
                auto pendingUploadChangedAt = Clock::now();
                unsigned int nextProposalId = 0;
                while (m_running.load(std::memory_order_acquire)) {
                    const auto now = Clock::now();
                    const unsigned int mask = m_localActiveMask.load(std::memory_order_acquire) & kMaximumActiveMask;
                    if (mask != lastSentMask ||
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSentAt).count() >= kStateHeartbeatMs) {
                        if (!SendJsonFrame(socket, json({
                            { "type", "state" }, { "activeMask", mask }
                        }).dump())) break;
                        lastSentMask = mask;
                        lastSentAt = now;
                    }

                    const bool subscriptionDesired =
                        m_teamSyncSubscriptionDesired.load(std::memory_order_acquire);
                    const unsigned int subscriptionId =
                        m_teamSyncSubscriptionId.load(std::memory_order_acquire);
                    const bool pushSupported = GetStatusSnapshot().teamSyncPushSupported;
                    if (pushSupported && subscriptionId != 0 &&
                        (subscriptionId != sentSubscriptionId ||
                            subscriptionDesired != sentSubscriptionEnabled)) {
                        if (!SendJsonFrame(socket, json({
                            { "type", "team_sync_subscribe" },
                            { "version", kProtocolVersion },
                            { "enabled", subscriptionDesired },
                            { "subscriptionId", subscriptionId }
                        }).dump())) break;
                        sentSubscriptionId = subscriptionId;
                        sentSubscriptionEnabled = subscriptionDesired;
                        lastReceivedPushRevision = 0;
                        {
                            std::lock_guard<std::mutex> lock(m_statusMutex);
                            m_status.teamSyncSubscribed = subscriptionDesired;
                        }
                        NotifyStateChanged();
                    }

                    const std::uint64_t currentLocalRevision =
                        m_teamSyncRevision.load(std::memory_order_acquire);
                    if (currentLocalRevision != lastObservedLocalRevision) {
                        lastObservedLocalRevision = currentLocalRevision;
                        pendingUploadRevision = currentLocalRevision;
                        pendingUploadChangedAt = now;
                    }
                    const KeyMappingLanStatusSnapshot uploadStatus = GetStatusSnapshot();
                    const bool canUpload = m_teamSyncAutoSend.load(std::memory_order_acquire) &&
                        uploadStatus.teamSyncBidirectionalSupported &&
                        uploadStatus.teamSyncClientWriteAllowed;
                    if (!canUpload) {
                        // Changes made before both sides authorize uploads form the
                        // new baseline and are never sent later by surprise.
                        lastUploadedRevision = pendingUploadRevision;
                    }
                    else if (pendingUploadRevision != lastUploadedRevision &&
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - pendingUploadChangedAt).count() >= kTeamSyncDebounceMs) {
                        std::string snapshotPayload;
                        {
                            std::lock_guard<std::mutex> lock(m_teamSyncSnapshotMutex);
                            snapshotPayload = m_teamSyncSnapshot;
                        }
                        try {
                            const json snapshot = json::parse(snapshotPayload);
                            unsigned int proposalId = ++nextProposalId;
                            if (proposalId == 0) proposalId = ++nextProposalId;
                            const std::string proposal = json({
                                { "type", "team_sync_propose" },
                                { "version", kProtocolVersion },
                                { "proposalId", proposalId },
                                { "clientRevision", pendingUploadRevision },
                                { "snapshot", snapshot }
                            }).dump();
                            if (proposal.size() > kMaximumPayloadBytes ||
                                !SendJsonFrame(socket, proposal)) break;
                            lastUploadedRevision = pendingUploadRevision;
                        }
                        catch (...) {
                            lastUploadedRevision = pendingUploadRevision;
                            NotifyTeamSyncMessage(json({
                                { "type", "team_sync_error" },
                                { "version", kProtocolVersion },
                                { "reason", "invalid_local_snapshot" }
                            }).dump());
                        }
                    }

                    const unsigned int requestedTeamSyncId =
                        m_teamSyncRequestId.load(std::memory_order_acquire);
                    if (m_teamSyncPending.load(std::memory_order_acquire) &&
                        requestedTeamSyncId != 0 && requestedTeamSyncId != sentTeamSyncRequestId) {
                        if (!SendJsonFrame(socket, json({
                            { "type", "team_sync_request" },
                            { "version", kProtocolVersion },
                            { "requestId", requestedTeamSyncId }
                        }).dump())) break;
                        sentTeamSyncRequestId = requestedTeamSyncId;
                        teamSyncRequestedAt = now;
                    }
                    else if (m_teamSyncPending.load(std::memory_order_acquire) &&
                        sentTeamSyncRequestId == requestedTeamSyncId &&
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - teamSyncRequestedAt).count() >= 4000) {
                        m_teamSyncPending.store(false, std::memory_order_release);
                        NotifyTeamSyncMessage(json({
                            { "type", "team_sync_error" },
                            { "version", kProtocolVersion },
                            { "requestId", requestedTeamSyncId },
                            { "reason", "timeout" }
                        }).dump());
                        sentTeamSyncRequestId = 0;
                    }

                    fd_set readSet;
                    FD_ZERO(&readSet);
                    FD_SET(socket, &readSet);
                    timeval timeout{ 0, kSocketPollMs * 1000 };
                    const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
                    if (ready == SOCKET_ERROR) break;
                    if (ready > 0 && FD_ISSET(socket, &readSet)) {
                        std::string payload;
                        if (!ReceiveJsonFrame(socket, payload)) break;
                        try {
                            const json message = json::parse(payload);
                            const std::string type = message.value("type", std::string());
                            if (type == "ping") {
                                if (!SendJsonFrame(socket, json({ { "type", "pong" } }).dump())) break;
                            }
                            else if (type == "team_sync_snapshot" || type == "team_sync_error") {
                                if (type == "team_sync_error" && message.value("automatic", false)) {
                                    const unsigned int currentSubscriptionId =
                                        m_teamSyncSubscriptionId.load(std::memory_order_acquire);
                                    if (m_teamSyncSubscriptionDesired.load(std::memory_order_acquire) &&
                                        message.value("subscriptionId", 0u) == currentSubscriptionId) {
                                        NotifyTeamSyncMessage(message.dump());
                                    }
                                    continue;
                                }
                                const unsigned int responseId = message.value("requestId", 0u);
                                const unsigned int pendingId = m_teamSyncRequestId.load(std::memory_order_acquire);
                                if (m_teamSyncPending.load(std::memory_order_acquire) &&
                                    responseId != 0 && responseId == pendingId) {
                                    m_teamSyncPending.store(false, std::memory_order_release);
                                    NotifyTeamSyncMessage(message.dump());
                                    sentTeamSyncRequestId = 0;
                                }
                            }
                            else if (type == "team_sync_push") {
                                const unsigned int currentSubscriptionId =
                                    m_teamSyncSubscriptionId.load(std::memory_order_acquire);
                                const std::uint64_t revision = message.value("revision", 0ull);
                                if (m_teamSyncSubscriptionDesired.load(std::memory_order_acquire) &&
                                    message.value("subscriptionId", 0u) == currentSubscriptionId &&
                                    revision > lastReceivedPushRevision) {
                                    lastReceivedPushRevision = revision;
                                    NotifyTeamSyncMessage(message.dump());
                                }
                            }
                            else if (type == "team_sync_write_policy") {
                                const bool allowed = message.value("allowed", false);
                                {
                                    std::lock_guard<std::mutex> lock(m_statusMutex);
                                    m_status.teamSyncClientWriteAllowed = allowed;
                                }
                                NotifyStateChanged();
                            }
                            else if (type == "team_sync_propose_result") {
                                NotifyTeamSyncMessage(message.dump());
                            }
                            else if (type != "pong") {
                                // Unknown messages are ignored so newer optional
                                // features cannot break the key-mask channel.
                            }
                        }
                        catch (...) {
                            break;
                        }
                    }
                }
            }
            CloseTrackedSocket(m_connectionSocket, socket);
            if (m_teamSyncPending.exchange(false, std::memory_order_acq_rel)) {
                NotifyTeamSyncMessage(json({
                    { "type", "team_sync_error" },
                    { "version", kProtocolVersion },
                    { "requestId", m_teamSyncRequestId.load(std::memory_order_acquire) },
                    { "reason", "disconnected" }
                }).dump());
            }
            if (m_running.load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(m_statusMutex);
                m_status.connected = false;
                m_status.reconnecting = true;
                m_status.status = "reconnecting";
                m_status.teamSyncSubscribed = false;
                m_status.remoteDeviceName.clear();
            }
            NotifyStateChanged();
        }

        for (int waited = 0; waited < kReconnectIntervalMs &&
            m_running.load(std::memory_order_acquire); waited += 100) {
            ::Sleep(100);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.connected = false;
        m_status.reconnecting = false;
        m_status.running = false;
        m_status.teamSyncSubscribed = false;
        m_status.teamSyncPushSupported = false;
        m_status.teamSyncBidirectionalSupported = false;
        m_status.teamSyncClientWriteAllowed = false;
        m_status.remoteDeviceName.clear();
    }
    NotifyStateChanged();
}

void KeyMappingLanService::DiscoveryThreadMain(unsigned short port)
{
    SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET || !TrackSocket(m_discoverySocket, socket)) {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.discovering = false;
        NotifyStateChanged();
        return;
    }

    const BOOL broadcast = TRUE;
    ::setsockopt(socket, SOL_SOCKET, SO_BROADCAST,
        reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = ::htonl(INADDR_ANY);
    local.sin_port = 0;
    if (::bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        CloseTrackedSocket(m_discoverySocket, socket);
        m_discoveryRunning.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_status.discovering = false;
        }
        NotifyStateChanged();
        return;
    }

    json request = { { "type", kDiscoveryRequest }, { "version", kProtocolVersion } };
    const std::string payload = request.dump();
    sockaddr_in broadcastAddress{};
    broadcastAddress.sin_family = AF_INET;
    broadcastAddress.sin_addr.s_addr = ::htonl(INADDR_BROADCAST);
    broadcastAddress.sin_port = ::htons(port);
    ::sendto(socket, payload.data(), static_cast<int>(payload.size()), 0,
        reinterpret_cast<const sockaddr*>(&broadcastAddress), sizeof(broadcastAddress));

    std::vector<KeyMappingLanServerInfo> discovered;
    std::set<std::string> seen;
    const auto deadline = Clock::now() + std::chrono::milliseconds(1500);
    while (m_discoveryRunning.load(std::memory_order_acquire) && Clock::now() < deadline) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket, &readSet);
        timeval timeout{ 0, 100000 };
        const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) break;
        if (ready <= 0) continue;
        sockaddr_in sender{};
        int senderLength = sizeof(sender);
        char buffer[2048] = {};
        const int count = ::recvfrom(socket, buffer, sizeof(buffer) - 1, 0,
            reinterpret_cast<sockaddr*>(&sender), &senderLength);
        if (count <= 0) continue;
        try {
            const json response = json::parse(std::string(buffer, count));
            if (response.value("type", std::string()) != "discovery" ||
                response.value("version", 0) != kProtocolVersion) continue;
            KeyMappingLanServerInfo item;
            item.id = response.value("serverId", std::string());
            item.name = response.value("name", std::string());
            item.address = SockaddrToString(sender);
            item.port = static_cast<unsigned short>(response.value("port", static_cast<int>(port)));
            const std::string key = item.id.empty() ? item.address : item.id;
            if (!item.address.empty() && seen.insert(key).second) discovered.push_back(std::move(item));
        }
        catch (...) {}
    }

    CloseTrackedSocket(m_discoverySocket, socket);
    m_discoveryRunning.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.discovering = false;
        m_status.discoveredServers = std::move(discovered);
    }
    NotifyStateChanged();
}
