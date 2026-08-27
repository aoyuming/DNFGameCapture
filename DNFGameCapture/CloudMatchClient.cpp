#include "pch.h"

#include "CloudMatchClient.h"
#include "CloudMatchProtocol.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using nlohmann::json;

namespace {

constexpr std::size_t kMaxCommandQueueSize = 64;
constexpr std::size_t kMaxPendingAcks = 64;
constexpr std::size_t kMaxInboundMessageQueueSize = 128;
constexpr std::size_t kMaxProtectedResultCapacity = 96;
constexpr auto kAckTimeout = std::chrono::seconds(8);
constexpr DWORD kNetworkTimeoutMs = 3000;
constexpr DWORD kReceivePollTimeoutMs = 200;
constexpr std::size_t kMaxRegistrationResponseBytes = 4096;
constexpr std::array<int, 5> kReconnectDelaysSeconds{ 1, 2, 5, 10, 20 };
constexpr std::uint64_t kSnapshotSizingAckId =
    (std::numeric_limits<std::uint64_t>::max)() - 1;

class WinHttpHandle
{
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) noexcept : handle_(handle) {}
    ~WinHttpHandle() { Reset(); }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : handle_(other.Release()) {}
    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept
    {
        if (this != &other) Reset(other.Release());
        return *this;
    }

    HINTERNET Get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }

    HINTERNET Release() noexcept
    {
        HINTERNET released = handle_;
        handle_ = nullptr;
        return released;
    }

    void Reset(HINTERNET replacement = nullptr) noexcept
    {
        if (handle_) WinHttpCloseHandle(handle_);
        handle_ = replacement;
    }

private:
    HINTERNET handle_ = nullptr;
};

template<typename Character>
void SecureClear(std::basic_string<Character>& value) noexcept
{
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(Character));
    }
    value.clear();
}

template<typename String>
class SecureClearGuard
{
public:
    explicit SecureClearGuard(String& value) noexcept : value_(value) {}
    ~SecureClearGuard() { SecureClear(value_); }

    SecureClearGuard(const SecureClearGuard&) = delete;
    SecureClearGuard& operator=(const SecureClearGuard&) = delete;

private:
    String& value_;
};

std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty() || text.size() >
        static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), required) != required) {
        return {};
    }
    return result;
}

std::string SanitizeServerText(const json& value, const char* key,
    std::string fallback = {})
{
    const auto found = value.find(key);
    if (found == value.end() || !found->is_string()) return fallback;
    std::string result = found->get<std::string>();
    for (char& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7f) character = ' ';
    }
    if (result.size() > 256) {
        std::size_t end = 256;
        while (end > 0 && end < result.size() &&
            (static_cast<unsigned char>(result[end]) & 0xc0) == 0x80) {
            --end;
        }
        result.resize(end);
    }
    return result;
}

std::string SanitizeServerCode(const json& value, const char* key,
    std::string fallback)
{
    std::string code = SanitizeServerText(value, key, std::move(fallback));
    if (code.empty() || code.size() > 64) return "server_error";
    for (const unsigned char character : code) {
        if (!((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '_' ||
            character == '-' || character == '.')) {
            return "server_error";
        }
    }
    return code;
}

std::uint64_t ExtractSnapshotClientRevision(std::string_view snapshotJson) noexcept
{
    try {
        const json snapshot = json::parse(snapshotJson.begin(), snapshotJson.end(),
            nullptr, false);
        if (snapshot.is_discarded() || !snapshot.is_object()) return 0;
        const auto found = snapshot.find("clientRevision");
        if (found == snapshot.end()) return 0;
        if (found->is_number_unsigned()) return found->get<std::uint64_t>();
        if (found->is_number_integer()) {
            const long long revision = found->get<long long>();
            return revision > 0 ? static_cast<std::uint64_t>(revision) : 0;
        }
    }
    catch (...) {
    }
    return 0;
}

std::wstring JoinUrlPath(const std::wstring& basePath, std::wstring_view suffix)
{
    if (basePath.empty()) return std::wstring(suffix);
    if (suffix.empty()) return basePath;
    if (basePath.back() == L'/' && suffix.front() == L'/') {
        return basePath + std::wstring(suffix.substr(1));
    }
    if (basePath.back() != L'/' && suffix.front() != L'/') {
        return basePath + L'/' + std::wstring(suffix);
    }
    return basePath + std::wstring(suffix);
}

bool QueryHttpStatus(HINTERNET request, DWORD& status)
{
    DWORD size = sizeof(status);
    return WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE;
}

} // namespace

class CloudMatchClient::Impl
{
public:
    using Clock = std::chrono::steady_clock;

    ~Impl()
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        Stop();
        std::lock_guard<std::mutex> lock(mutex);
        callback = {};
        ClearCommandsLocked();
        ClearLatestSnapshotLocked();
        ClearPendingAcksLocked();
        ClearDesiredRoomLocked();
        ClearInboundMessagesLocked();
        ClearConfigLocked(configGeneration.load(std::memory_order_acquire));
    }

    bool Configure(const std::wstring& serverUrl, const std::string& deviceId,
        const std::string& deviceToken)
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        Config parsed;
        const bool validUrl = ParseServerUrl(serverUrl, parsed);
        parsed.deviceId = deviceId;
        parsed.deviceToken = deviceToken;
        parsed.credentialsValid = validUrl && !deviceId.empty() && !deviceToken.empty();
        return ApplyConfig(std::move(parsed), validUrl);
    }

    bool Start()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (started) return true;
        if (stopping) return false;
        stopRequested.store(false, std::memory_order_release);
        try {
            worker = std::thread(&Impl::WorkerMain, this);
            started = true;
            return true;
        }
        catch (...) {
            status.statusText = "worker_start_failed";
            return false;
        }
    }

    void Stop()
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        std::thread joiningThread;
        std::uint64_t stopGeneration = 0;
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (stopping) {
                condition.wait(lock, [&]() { return !stopping; });
                return;
            }
            stopping = true;
            stopRequested.store(true, std::memory_order_release);
            stopGeneration = configGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (started) joiningThread = std::move(worker);
        }

        CancelActiveOperation();
        condition.notify_all();
        if (joiningThread.joinable()) joiningThread.join();

        {
            std::lock_guard<std::mutex> lock(mutex);
            started = false;
            stopping = false;
            stopRequested.store(false, std::memory_order_release);
            ClearCommandsLocked();
            ClearLatestSnapshotLocked();
            ClearPendingAcksLocked();
            ClearDesiredRoomLocked();
            ClearInboundMessagesLocked();
            ResetProtectedCapacityLocked();
            ClearConfigLocked(stopGeneration);
            status = {};
            status.statusText = "stopped";
        }
        condition.notify_all();
    }

    bool RegisterDevice(const std::string& deviceId)
    {
        if (deviceId.empty() || deviceId.size() > 128) return false;
        return EnqueueCommand(CommandKind::registerDevice, deviceId, {}, {});
    }

    bool JoinRoom(const std::string& roomId, const std::string& broadcasterName)
    {
        if (roomId.empty() || roomId.size() > 64 || broadcasterName.empty() ||
            broadcasterName.size() > 128) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!JoinRoomLocked(config.generation, roomId, broadcasterName)) return false;
        }
        condition.notify_all();
        return true;
    }

    bool Rename(const std::string& broadcasterName)
    {
        if (broadcasterName.empty() || broadcasterName.size() > 128) return false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const std::uint64_t generation = config.generation;
            if (!EnqueueCommandLocked(generation, CommandKind::renameRoom,
                broadcasterName, {}, {})) {
                return false;
            }
            if (desiredRoom && desiredRoom->generation == generation) {
                desiredRoom->broadcasterName = broadcasterName;
            }
        }
        condition.notify_all();
        return true;
    }

    bool LeaveRoom()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!EnqueueCommandLocked(config.generation, CommandKind::leaveRoom,
                {}, {}, {})) {
                return false;
            }
            ClearDesiredRoomLocked();
        }
        condition.notify_all();
        return true;
    }

    bool UploadSnapshot(std::string snapshotJson)
    {
        std::uint64_t generation = 0;
        const std::uint64_t clientRevision = ExtractSnapshotClientRevision(snapshotJson);
        std::size_t outboundLimit = cloud_match::kMaxCloudMatchPayloadBytes;
        {
            std::lock_guard<std::mutex> lock(mutex);
            generation = config.generation;
            outboundLimit = currentOutboundLimit;
        }

        std::string sizingPacket;
        const cloud_match::SnapshotUploadEncodeResult encoded =
            cloud_match::EncodeSnapshotUploadEvent(snapshotJson, kSnapshotSizingAckId,
                outboundLimit, sizingPacket);
        SecureClear(sizingPacket);
        if (encoded != cloud_match::SnapshotUploadEncodeResult::success) {
            bool protectedResultReservation = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (config.generation != generation ||
                    !ReserveProtectedResultLocked()) {
                    SecureClear(snapshotJson);
                    return false;
                }
                protectedResultReservation = true;
            }
            SecureClear(snapshotJson);
            NotifySnapshotUploadFailure(generation, clientRevision, encoded,
                &protectedResultReservation);
            return false;
        }

        std::optional<PendingSnapshot> canceledSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != generation) {
                SecureClear(snapshotJson);
                return false;
            }
            if (!config.credentialsValid) {
                status.statusText = "not_configured";
                SecureClear(snapshotJson);
                return false;
            }
            if (!ReserveProtectedResultLocked()) {
                SecureClear(snapshotJson);
                return false;
            }
            if (latestSnapshot) {
                canceledSnapshot = std::move(latestSnapshot);
                latestSnapshot.reset();
            }
            latestSnapshot.emplace(generation, clientRevision,
                std::move(snapshotJson), true);
        }
        if (canceledSnapshot) {
            NotifySnapshotCanceled(*canceledSnapshot);
        }
        condition.notify_all();
        return true;
    }

    bool RequestComparison(const std::string& requestId)
    {
        if (requestId.empty() || requestId.size() > 128) return false;
        return EnqueueCommand(CommandKind::requestComparison, {}, {}, requestId);
    }

    bool RequestSnapshot(const std::string& requestId, const std::string& targetDeviceId)
    {
        if (requestId.empty() || requestId.size() > 128 || targetDeviceId.empty() ||
            targetDeviceId.size() > 128) {
            return false;
        }
        return EnqueueCommand(CommandKind::requestSnapshot, targetDeviceId, {}, requestId);
    }

    CloudMatchStatusSnapshot GetStatusSnapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return status;
    }

    void SetMessageCallback(MessageCallback newCallback)
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        std::lock_guard<std::mutex> lock(mutex);
        callback = std::move(newCallback);
    }

    std::size_t DispatchMessages(std::size_t maxCount)
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        std::size_t dispatched = 0;
        while (dispatched < maxCount) {
            InboundMessage message;
            MessageCallback copiedCallback;
            bool currentGeneration = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (inboundMessages.empty()) break;
                message = std::move(inboundMessages.front());
                inboundMessages.pop_front();
                if (message.protectedResult && protectedResultsQueued > 0) {
                    --protectedResultsQueued;
                }
                currentGeneration = message.generation == config.generation;
                if (currentGeneration) copiedCallback = callback;
            }
            condition.notify_all();

            if (currentGeneration && copiedCallback) {
                std::string callbackMessage = message.serialized;
                copiedCallback(std::move(callbackMessage));
                SecureClear(callbackMessage);
            }
            SecureClear(message.serialized);
            ++dispatched;
        }
        return dispatched;
    }

#ifdef CLOUD_MATCH_CLIENT_STANDALONE
    std::uint64_t ConfigureForTesting()
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        Config parsed;
        parsed.serverValid = true;
        parsed.credentialsValid = true;
        parsed.port = INTERNET_DEFAULT_HTTP_PORT;
        parsed.host = L"test.invalid";
        parsed.deviceId = "test-device";
        parsed.deviceToken = "test-token";
        ApplyConfig(std::move(parsed), true);
        return configGeneration.load(std::memory_order_acquire);
    }

    bool CompleteNextProtectedOperationForTesting()
    {
        Command command;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (commands.empty()) return false;
            command = std::move(commands.front());
            commands.pop_front();
        }

        std::string type;
        switch (command.kind) {
        case CommandKind::registerDevice:
            type = "device_registration_error";
            break;
        case CommandKind::joinRoom:
            type = "room_join_result";
            break;
        case CommandKind::renameRoom:
            type = "room_rename_result";
            break;
        case CommandKind::leaveRoom:
            type = "room_leave_result";
            break;
        case CommandKind::uploadSnapshot:
            type = "snapshot_upload_result";
            break;
        case CommandKind::requestComparison:
            type = "room_comparison_result";
            break;
        case CommandKind::requestSnapshot:
            type = "snapshot_result";
            break;
        }
        json result = {
            { "type", type },
            { "ok", false },
            { "code", "test_complete" }
        };
        if (!command.requestId.empty()) result["requestId"] = command.requestId;
        NotifyJson(command.generation, result, {}, false,
            &command.protectedResultReservation);
        return true;
    }

    bool CompleteLatestSnapshotAckForTesting(bool ok,
        std::uint64_t acceptedRevision, const std::string& code)
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        Config activeConfig;
        std::uint64_t ackId = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!latestSnapshot || latestSnapshot->generation != config.generation ||
                nextAckId == (std::numeric_limits<std::uint64_t>::max)()) {
                return false;
            }
            PendingSnapshot snapshot = std::move(*latestSnapshot);
            latestSnapshot.reset();
            ackId = nextAckId++;
            pendingAcks.emplace(ackId, PendingAck{
                CommandKind::uploadSnapshot, "snapshot_upload_result", {},
                snapshot.clientRevision, Clock::now() + kAckTimeout,
                snapshot.protectedResultReservation
            });
            snapshot.protectedResultReservation = false;
            activeConfig = config;
        }

        cloud_match::SocketIoAck ack;
        ack.id = ackId;
        if (ok) {
            ack.payload = {
                { "ok", true }, { "acceptedRevision", acceptedRevision }
            };
        }
        else {
            ack.payload = {
                { "ok", false },
                { "code", code.empty() ? "server_error" : code }
            };
        }
        HandleAck(activeConfig, ack);
        return true;
    }

    bool JoinRoomForGenerationForTesting(std::uint64_t generation,
        const std::string& roomId, const std::string& broadcasterName)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return JoinRoomLocked(generation, roomId, broadcasterName);
    }

    bool HasDesiredRoomForTesting() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return desiredRoom.has_value();
    }

    std::uint64_t DesiredRoomGenerationForTesting() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return desiredRoom ? desiredRoom->generation : 0;
    }

    void SetDesiredJoinForReplayForTesting(const std::string& roomId,
        const std::string& broadcasterName)
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        std::lock_guard<std::mutex> lock(mutex);
        desiredRoom = DesiredRoom{ roomId, broadcasterName, config.generation };
        desiredJoinPendingGeneration = config.generation;
        desiredJoinInFlightGeneration.reset();
    }

    std::string RetryRememberedJoinForTesting()
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        const std::uint64_t generation = configGeneration.load(std::memory_order_acquire);
        Command join;
        switch (PrepareRememberedJoin(generation, join)) {
        case RememberedJoinPreparation::noDesired:
            return "no_desired";
        case RememberedJoinPreparation::alreadyInFlight:
            return "already_in_flight";
        case RememberedJoinPreparation::noCapacity:
            return "no_capacity";
        case RememberedJoinPreparation::ready:
            break;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != generation ||
                nextAckId == (std::numeric_limits<std::uint64_t>::max)()) {
                if (join.protectedResultReservation && protectedResultReservations > 0) {
                    --protectedResultReservations;
                    join.protectedResultReservation = false;
                }
                return "network_failure";
            }
            const std::uint64_t ackId = nextAckId++;
            pendingAcks.emplace(ackId, PendingAck{
                CommandKind::joinRoom, "room_join_result", {},
                0, Clock::now() + kAckTimeout, join.protectedResultReservation
            });
            join.protectedResultReservation = false;
            desiredJoinInFlightGeneration = generation;
            ++rememberedJoinSendCountForTesting;
        }
        return "sent";
    }

    std::size_t RememberedJoinSendCountForTesting() const
    {
        std::lock_guard<std::recursive_mutex> dispatchLock(dispatchGate);
        std::lock_guard<std::mutex> lock(mutex);
        return rememberedJoinSendCountForTesting;
    }
#endif

private:
    enum class CommandKind
    {
        registerDevice,
        joinRoom,
        renameRoom,
        leaveRoom,
        uploadSnapshot,
        requestComparison,
        requestSnapshot
    };

    struct Config
    {
        Config() = default;
        ~Config() { Clear(); }

        Config(const Config& other)
        {
            CopyFrom(other);
        }

        Config& operator=(const Config& other)
        {
            if (this != &other) {
                Clear();
                CopyFrom(other);
            }
            return *this;
        }

        Config(Config&& other) noexcept
        {
            Swap(other);
        }

        Config& operator=(Config&& other) noexcept
        {
            if (this != &other) {
                Clear();
                Swap(other);
            }
            return *this;
        }

        void Clear() noexcept
        {
            serverValid = false;
            credentialsValid = false;
            secure = false;
            port = 0;
            SecureClear(host);
            SecureClear(basePath);
            SecureClear(deviceId);
            SecureClear(deviceToken);
            generation = 0;
        }

        bool serverValid = false;
        bool credentialsValid = false;
        bool secure = false;
        INTERNET_PORT port = 0;
        std::wstring host;
        std::wstring basePath;
        std::string deviceId;
        std::string deviceToken;
        std::uint64_t generation = 0;

    private:
        void CopyFrom(const Config& other)
        {
            serverValid = other.serverValid;
            credentialsValid = other.credentialsValid;
            secure = other.secure;
            port = other.port;
            host = other.host;
            basePath = other.basePath;
            deviceId = other.deviceId;
            deviceToken = other.deviceToken;
            generation = other.generation;
        }

        void Swap(Config& other) noexcept
        {
            std::swap(serverValid, other.serverValid);
            std::swap(credentialsValid, other.credentialsValid);
            std::swap(secure, other.secure);
            std::swap(port, other.port);
            host.swap(other.host);
            basePath.swap(other.basePath);
            deviceId.swap(other.deviceId);
            deviceToken.swap(other.deviceToken);
            std::swap(generation, other.generation);
        }
    };

    struct Command
    {
        Command() = default;
        ~Command() { Clear(); }

        Command(const Command&) = delete;
        Command& operator=(const Command&) = delete;

        Command(Command&& other) noexcept
            : kind(other.kind), generation(other.generation), sequence(other.sequence),
            protectedResultReservation(other.protectedResultReservation)
        {
            argument.swap(other.argument);
            secondArgument.swap(other.secondArgument);
            requestId.swap(other.requestId);
            other.generation = 0;
            other.sequence = 0;
            other.protectedResultReservation = false;
        }

        Command& operator=(Command&& other) noexcept
        {
            if (this != &other) {
                Clear();
                kind = other.kind;
                generation = other.generation;
                sequence = other.sequence;
                protectedResultReservation = other.protectedResultReservation;
                argument.swap(other.argument);
                secondArgument.swap(other.secondArgument);
                requestId.swap(other.requestId);
                other.generation = 0;
                other.sequence = 0;
                other.protectedResultReservation = false;
            }
            return *this;
        }

        CommandKind kind = CommandKind::joinRoom;
        std::string argument;
        std::string secondArgument;
        std::string requestId;
        std::uint64_t generation = 0;
        std::uint64_t sequence = 0;
        bool protectedResultReservation = false;

        void Clear() noexcept
        {
            SecureClear(argument);
            SecureClear(secondArgument);
            SecureClear(requestId);
            generation = 0;
            sequence = 0;
            protectedResultReservation = false;
        }
    };

    struct DesiredRoom
    {
        std::string roomId;
        std::string broadcasterName;
        std::uint64_t generation = 0;

        ~DesiredRoom()
        {
            SecureClear(roomId);
            SecureClear(broadcasterName);
        }
    };

    struct PendingSnapshot
    {
        PendingSnapshot() = default;
        PendingSnapshot(std::uint64_t sourceGeneration,
            std::uint64_t sourceClientRevision, std::string sourceJson,
            bool sourceReservation)
            : generation(sourceGeneration),
            clientRevision(sourceClientRevision),
            protectedResultReservation(sourceReservation)
        {
            jsonText.swap(sourceJson);
            SecureClear(sourceJson);
        }
        ~PendingSnapshot() { SecureClear(jsonText); }

        PendingSnapshot(const PendingSnapshot&) = delete;
        PendingSnapshot& operator=(const PendingSnapshot&) = delete;

        PendingSnapshot(PendingSnapshot&& other) noexcept
            : generation(other.generation),
            clientRevision(other.clientRevision),
            protectedResultReservation(other.protectedResultReservation)
        {
            jsonText.swap(other.jsonText);
            other.generation = 0;
            other.clientRevision = 0;
            other.protectedResultReservation = false;
        }

        PendingSnapshot& operator=(PendingSnapshot&& other) noexcept
        {
            if (this != &other) {
                SecureClear(jsonText);
                generation = other.generation;
                clientRevision = other.clientRevision;
                protectedResultReservation = other.protectedResultReservation;
                jsonText.swap(other.jsonText);
                other.generation = 0;
                other.clientRevision = 0;
                other.protectedResultReservation = false;
            }
            return *this;
        }

        std::uint64_t generation = 0;
        std::uint64_t clientRevision = 0;
        std::string jsonText;
        bool protectedResultReservation = false;
    };

    struct PendingAck
    {
        CommandKind kind = CommandKind::joinRoom;
        std::string resultType;
        std::string requestId;
        std::uint64_t clientRevision = 0;
        Clock::time_point deadline;
        bool protectedResultReservation = false;

        void Clear() noexcept
        {
            SecureClear(resultType);
            SecureClear(requestId);
            clientRevision = 0;
            protectedResultReservation = false;
        }
    };

    struct InboundMessage
    {
        InboundMessage() = default;
        ~InboundMessage()
        {
            SecureClear(type);
            SecureClear(coalesceKey);
            SecureClear(serialized);
        }

        InboundMessage(const InboundMessage&) = delete;
        InboundMessage& operator=(const InboundMessage&) = delete;

        InboundMessage(InboundMessage&& other) noexcept
            : generation(other.generation), coalescible(other.coalescible),
            overflowError(other.overflowError), sensitive(other.sensitive),
            protectedResult(other.protectedResult)
        {
            type.swap(other.type);
            coalesceKey.swap(other.coalesceKey);
            serialized.swap(other.serialized);
            other.generation = 0;
            other.coalescible = false;
            other.overflowError = false;
            other.sensitive = false;
            other.protectedResult = false;
        }

        InboundMessage& operator=(InboundMessage&& other) noexcept
        {
            if (this != &other) {
                SecureClear(type);
                SecureClear(coalesceKey);
                SecureClear(serialized);
                generation = other.generation;
                coalescible = other.coalescible;
                overflowError = other.overflowError;
                sensitive = other.sensitive;
                protectedResult = other.protectedResult;
                type.swap(other.type);
                coalesceKey.swap(other.coalesceKey);
                serialized.swap(other.serialized);
                other.generation = 0;
                other.coalescible = false;
                other.overflowError = false;
                other.sensitive = false;
                other.protectedResult = false;
            }
            return *this;
        }

        std::uint64_t generation = 0;
        std::string type;
        std::string coalesceKey;
        std::string serialized;
        bool coalescible = false;
        bool overflowError = false;
        bool sensitive = false;
        bool protectedResult = false;
    };

    struct WebSocketConnection
    {
        WinHttpHandle session;
        WinHttpHandle connect;
        WinHttpHandle socket;
        cloud_match::WebSocketTextAssembler assembler;
        std::uint32_t pingIntervalMs = 25000;
        std::uint32_t pingTimeoutMs = 20000;
        std::size_t outboundLimit = cloud_match::kMaxCloudMatchPayloadBytes;
        Clock::time_point lastServerActivity = Clock::now();
    };

    enum class ReceiveResult
    {
        poll,
        message,
        closed,
        rejected,
        failed
    };

    enum class CancelablePublishResult
    {
        published,
        notPublished,
        cancelled
    };

    enum class SnapshotSendResult
    {
        sent,
        rejected,
        transportFailure
    };

    enum class RememberedJoinResult
    {
        noDesired,
        alreadyInFlight,
        noCapacity,
        sent,
        networkFailure
    };

    enum class RememberedJoinPreparation
    {
        noDesired,
        alreadyInFlight,
        noCapacity,
        ready
    };

    static bool IsCoalescibleNotification(std::string_view type) noexcept
    {
        return type == "room_changed" || type == "room_presence";
    }

    static bool IsProtectedInboundResult(std::string_view type) noexcept
    {
        return type == "device_registered" || type == "device_registration_error" ||
            type == "room_join_result" || type == "snapshot_upload_result" ||
            type == "room_rename_result" || type == "room_leave_result" ||
            type == "room_comparison_result" || type == "snapshot_result";
    }

    bool DropOldestNotificationLocked() noexcept
    {
        const auto notification = std::find_if(inboundMessages.begin(),
            inboundMessages.end(), [](const InboundMessage& queued) {
                return !queued.protectedResult;
            });
        if (notification == inboundMessages.end()) return false;
        inboundMessages.erase(notification);
        return true;
    }

    bool ReserveProtectedResultLocked(bool reportQueueFull = true)
    {
        if (protectedResultsQueued + protectedResultReservations >=
            kMaxProtectedResultCapacity) {
            if (reportQueueFull) status.statusText = "queue_full";
            return false;
        }
        while (inboundMessages.size() + protectedResultReservations >=
            kMaxInboundMessageQueueSize) {
            if (!DropOldestNotificationLocked()) {
                if (reportQueueFull) status.statusText = "queue_full";
                return false;
            }
        }
        ++protectedResultReservations;
        return true;
    }

    void ResetProtectedCapacityLocked() noexcept
    {
        protectedResultsQueued = 0;
        protectedResultReservations = 0;
    }

    void ClearCommandsLocked() noexcept
    {
        for (Command& command : commands) command.Clear();
        commands.clear();
    }

    void ClearLatestSnapshotLocked() noexcept
    {
        latestSnapshot.reset();
    }

    void ClearPendingAcksLocked() noexcept
    {
        for (auto& entry : pendingAcks) entry.second.Clear();
        pendingAcks.clear();
        nextAckId = 1;
    }

    void ClearDesiredRoomLocked() noexcept
    {
        desiredRoom.reset();
        desiredJoinPendingGeneration.reset();
        desiredJoinInFlightGeneration.reset();
    }

    void ClearInboundMessagesLocked() noexcept
    {
        inboundMessages.clear();
        protectedResultsQueued = 0;
    }

    void ClearConfigLocked(std::uint64_t generation) noexcept
    {
        SecureClear(config.deviceToken);
        config.Clear();
        config.generation = generation;
        currentOutboundLimit = cloud_match::kMaxCloudMatchPayloadBytes;
    }

    void CancelActiveOperation() noexcept
    {
        HINTERNET activeHandle = activeCancelableHandle.exchange(nullptr);
        if (activeHandle) WinHttpCloseHandle(activeHandle);
    }

    CancelablePublishResult PublishCancelableHandle(HINTERNET handle,
        std::uint64_t generation) noexcept
    {
        if (!handle || stopRequested.load(std::memory_order_acquire) ||
            configGeneration.load(std::memory_order_acquire) != generation) {
            return CancelablePublishResult::notPublished;
        }

        HINTERNET expected = nullptr;
        if (!activeCancelableHandle.compare_exchange_strong(expected, handle,
            std::memory_order_acq_rel)) {
            return CancelablePublishResult::notPublished;
        }
        if (!stopRequested.load(std::memory_order_acquire) &&
            configGeneration.load(std::memory_order_acquire) == generation) {
            return CancelablePublishResult::published;
        }

        expected = handle;
        if (activeCancelableHandle.compare_exchange_strong(expected, nullptr,
            std::memory_order_acq_rel)) {
            WinHttpCloseHandle(handle);
        }
        return CancelablePublishResult::cancelled;
    }

    bool ReleaseCancelableHandle(HINTERNET handle) noexcept
    {
        HINTERNET expected = handle;
        return activeCancelableHandle.compare_exchange_strong(expected, nullptr,
            std::memory_order_acq_rel);
    }

    class ActiveHandleScope
    {
    public:
        ActiveHandleScope(Impl& owner, WinHttpHandle& handleOwner,
            std::uint64_t generation) noexcept
            : owner_(owner), handleOwner_(handleOwner)
        {
            const CancelablePublishResult result =
                owner_.PublishCancelableHandle(handleOwner_.Get(), generation);
            active_ = result == CancelablePublishResult::published;
            if (result == CancelablePublishResult::cancelled) handleOwner_.Release();
        }

        ~ActiveHandleScope() { Finish(); }

        ActiveHandleScope(const ActiveHandleScope&) = delete;
        ActiveHandleScope& operator=(const ActiveHandleScope&) = delete;

        explicit operator bool() const noexcept { return active_; }

        void Finish() noexcept
        {
            if (!active_) return;
            if (!owner_.ReleaseCancelableHandle(handleOwner_.Get())) {
                handleOwner_.Release();
            }
            active_ = false;
        }

    private:
        Impl& owner_;
        WinHttpHandle& handleOwner_;
        bool active_ = false;
    };

    static bool ParseServerUrl(const std::wstring& url, Config& result)
    {
        if (url.empty() || url.size() > 2048 || url.find(L'\0') != std::wstring::npos) {
            return false;
        }
        std::wstring mutableUrl = url;
        SecureClearGuard mutableUrlGuard(mutableUrl);
        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUserNameLength = static_cast<DWORD>(-1);
        components.dwPasswordLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(mutableUrl.data(), static_cast<DWORD>(mutableUrl.size()),
            ICU_DECODE, &components)) {
            return false;
        }
        if ((components.nScheme != INTERNET_SCHEME_HTTP &&
            components.nScheme != INTERNET_SCHEME_HTTPS) ||
            components.dwHostNameLength == 0 || components.dwUserNameLength != 0 ||
            components.dwPasswordLength != 0 || components.dwExtraInfoLength != 0) {
            return false;
        }

        result.serverValid = true;
        result.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
        result.port = components.nPort;
        result.host.assign(components.lpszHostName, components.dwHostNameLength);
        if (components.dwUrlPathLength > 0) {
            result.basePath.assign(components.lpszUrlPath, components.dwUrlPathLength);
        }
        if (result.basePath == L"/") result.basePath.clear();
        while (!result.basePath.empty() && result.basePath.back() == L'/') {
            result.basePath.pop_back();
        }
        return !result.host.empty();
    }

    bool ApplyConfig(Config&& parsed, bool validUrl)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            const std::uint64_t generation =
                configGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            parsed.generation = generation;
            ClearCommandsLocked();
            ClearLatestSnapshotLocked();
            ClearPendingAcksLocked();
            ClearDesiredRoomLocked();
            ClearInboundMessagesLocked();
            ResetProtectedCapacityLocked();
            CancelActiveOperation();
            ClearConfigLocked(generation);
            config = std::move(parsed);
            status = {};
            status.configured = config.credentialsValid;
            status.statusText = validUrl ?
                (config.credentialsValid ? "configured" : "credentials_required") :
                "invalid_server_url";
#ifdef CLOUD_MATCH_CLIENT_STANDALONE
            rememberedJoinSendCountForTesting = 0;
#endif
        }
        condition.notify_all();
        return validUrl;
    }

    bool EnqueueCommandLocked(std::uint64_t generation, CommandKind kind,
        std::string argument, std::string secondArgument, std::string requestId)
    {
        const bool registration = kind == CommandKind::registerDevice;
        if (stopping || config.generation != generation ||
            configGeneration.load(std::memory_order_acquire) != generation ||
            !config.serverValid || (!registration && !config.credentialsValid)) {
            status.statusText = "not_configured";
            return false;
        }
        if (commands.size() >= kMaxCommandQueueSize) {
            status.statusText = "queue_full";
            return false;
        }
        if (!ReserveProtectedResultLocked()) return false;

        Command command;
        command.kind = kind;
        command.argument = std::move(argument);
        command.secondArgument = std::move(secondArgument);
        command.requestId = std::move(requestId);
        command.generation = generation;
        command.sequence = nextCommandSequence++;
        command.protectedResultReservation = true;
        commands.push_back(std::move(command));
        return true;
    }

    bool JoinRoomLocked(std::uint64_t generation, const std::string& roomId,
        const std::string& broadcasterName)
    {
        if (config.generation != generation ||
            !EnqueueCommandLocked(generation, CommandKind::joinRoom, roomId,
                broadcasterName, {})) {
            return false;
        }
        desiredRoom = DesiredRoom{ roomId, broadcasterName, generation };
        desiredJoinPendingGeneration = generation;
        desiredJoinInFlightGeneration.reset();
        return true;
    }

    bool EnqueueCommand(CommandKind kind, std::string argument,
        std::string secondArgument, std::string requestId)
    {
        bool enqueued = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            enqueued = EnqueueCommandLocked(config.generation, kind,
                std::move(argument), std::move(secondArgument), std::move(requestId));
        }
        if (enqueued) condition.notify_all();
        return enqueued;
    }

    void SetQueueStatus(const char* text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        status.statusText = text;
    }

    Config CopyConfig() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return config;
    }

    bool ShouldAbort(std::uint64_t generation) const
    {
        return stopRequested.load(std::memory_order_acquire) ||
            configGeneration.load(std::memory_order_acquire) != generation;
    }

    bool ShouldStop() const noexcept
    {
        return stopRequested.load(std::memory_order_acquire);
    }

    void UpdateStatus(std::uint64_t generation,
        const std::function<void(CloudMatchStatusSnapshot&)>& update)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (config.generation != generation) return;
        update(status);
    }

    void UpdateOutboundLimit(std::uint64_t generation, std::size_t limit)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (config.generation != generation) return;
        currentOutboundLimit = (std::min)(limit,
            cloud_match::kMaxCloudMatchPayloadBytes);
    }

    void EnqueueInboundMessage(std::uint64_t generation, std::string type,
        std::string serialized, std::string coalesceKey, bool sensitive,
        bool* protectedResultReservation)
    {
        InboundMessage incoming;
        incoming.generation = generation;
        incoming.coalescible = IsCoalescibleNotification(type);
        incoming.sensitive = sensitive;
        incoming.protectedResult = IsProtectedInboundResult(type);
        incoming.type.swap(type);
        incoming.coalesceKey.swap(coalesceKey);
        incoming.serialized.swap(serialized);

        std::lock_guard<std::mutex> lock(mutex);
        if (config.generation != generation) return;

        if (incoming.protectedResult) {
            if (!protectedResultReservation || !*protectedResultReservation ||
                protectedResultReservations == 0) {
                return;
            }
            --protectedResultReservations;
            ++protectedResultsQueued;
            *protectedResultReservation = false;
            inboundMessages.push_back(std::move(incoming));
            return;
        }

        if (incoming.coalescible) {
            const auto existing = std::find_if(inboundMessages.rbegin(), inboundMessages.rend(),
                [&incoming](const InboundMessage& queued) {
                    return queued.generation == incoming.generation &&
                        queued.type == incoming.type &&
                        queued.coalesceKey == incoming.coalesceKey;
                });
            if (existing != inboundMessages.rend()) {
                SecureClear(existing->serialized);
                existing->serialized.swap(incoming.serialized);
                existing->sensitive = incoming.sensitive;
                return;
            }
        }

        if (inboundMessages.size() + protectedResultReservations >=
            kMaxInboundMessageQueueSize && !DropOldestNotificationLocked()) {
            return;
        }
        inboundMessages.push_back(std::move(incoming));
    }

    void NotifyJson(std::uint64_t generation, const json& message,
        std::string coalesceKey = {}, bool sensitive = false,
        bool* protectedResultReservation = nullptr)
    {
        std::string serialized;
        std::string type;
        try {
            const auto foundType = message.find("type");
            if (foundType == message.end() || !foundType->is_string()) {
                return;
            }
            type = foundType->get<std::string>();
            serialized = message.dump();
        }
        catch (...) {
            SecureClear(serialized);
        }
        if (serialized.empty() ||
            serialized.size() > cloud_match::kMaxCloudMatchPayloadBytes) {
            SecureClear(serialized);
            if (!protectedResultReservation || !*protectedResultReservation ||
                !IsProtectedInboundResult(type)) {
                SecureClear(type);
                return;
            }
            try {
                serialized = json{
                    { "type", type },
                    { "ok", false },
                    { "code", "payload_too_large" }
                }.dump();
            }
            catch (...) {
                SecureClear(type);
                SecureClear(serialized);
                return;
            }
        }
        EnqueueInboundMessage(generation, std::move(type), std::move(serialized),
            std::move(coalesceKey), sensitive, protectedResultReservation);
    }

    void NotifySnapshotUploadFailure(std::uint64_t generation,
        std::uint64_t clientRevision,
        cloud_match::SnapshotUploadEncodeResult result,
        bool* protectedResultReservation)
    {
        const char* code = result == cloud_match::SnapshotUploadEncodeResult::payloadTooLarge ?
            "payload_too_large" : "invalid_payload";
        NotifyJson(generation, json{
            { "type", "snapshot_upload_result" },
            { "ok", false },
            { "code", code },
            { "clientRevision", clientRevision }
        }, {}, false, protectedResultReservation);
    }

    void NotifySnapshotCanceled(PendingSnapshot& snapshot)
    {
        NotifyJson(snapshot.generation, json{
            { "type", "snapshot_upload_result" },
            { "ok", false },
            { "code", "canceled" },
            { "clientRevision", snapshot.clientRevision }
        }, {}, false, &snapshot.protectedResultReservation);
    }

    void NotifyCloudError(std::uint64_t generation, std::string code,
        std::string message = {})
    {
        json output = {
            { "type", "cloud_error" },
            { "ok", false },
            { "code", std::move(code) }
        };
        if (!message.empty()) output["message"] = std::move(message);
        NotifyJson(generation, output);
    }

    bool PopRegistrationCommand(std::uint64_t generation, Command& command)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = std::find_if(commands.begin(), commands.end(),
            [generation](const Command& queued) {
                return queued.generation == generation &&
                    queued.kind == CommandKind::registerDevice;
            });
        if (found == commands.end()) return false;
        command = std::move(*found);
        commands.erase(found);
        return true;
    }

    bool HasRegistrationCommand(std::uint64_t generation) const
    {
        return std::any_of(commands.begin(), commands.end(),
            [generation](const Command& queued) {
                return queued.generation == generation &&
                    queued.kind == CommandKind::registerDevice;
            });
    }

    void ProcessRegistrationCommands(const Config& activeConfig)
    {
        Command command;
        while (!ShouldAbort(activeConfig.generation) &&
            PopRegistrationCommand(activeConfig.generation, command)) {
            RegisterDeviceOnWorker(activeConfig, command);
        }
    }

    bool ReadBoundedResponse(HINTERNET request, std::string& body,
        std::uint64_t generation)
    {
        SecureClear(body);
        for (;;) {
            if (ShouldAbort(generation)) return false;
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || ShouldAbort(generation)) {
                return false;
            }
            if (available == 0) return true;
            if (available > kMaxRegistrationResponseBytes - body.size()) return false;

            const std::size_t offset = body.size();
            body.resize(offset + available);
            DWORD downloaded = 0;
            if (!WinHttpReadData(request, body.data() + offset, available, &downloaded) ||
                ShouldAbort(generation)) {
                return false;
            }
            body.resize(offset + downloaded);
            if (downloaded == 0) return true;
        }
    }

    void RegisterDeviceOnWorker(const Config& activeConfig, Command& command)
    {
        const std::string& deviceId = command.argument;
        json requestJson = { { "deviceId", deviceId } };
        std::string requestBody;
        SecureClearGuard requestBodyGuard(requestBody);
        try {
            requestBody = requestJson.dump();
        }
        catch (...) {
            NotifyRegistrationError(activeConfig.generation, "invalid_request", command);
            return;
        }

        WinHttpHandle session(WinHttpOpen(L"DNFGameCapture CloudMatch/1",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session || !WinHttpSetTimeouts(session.Get(), kNetworkTimeoutMs,
            kNetworkTimeoutMs, kNetworkTimeoutMs, kNetworkTimeoutMs)) {
            NotifyRegistrationError(activeConfig.generation, "network_error", command);
            return;
        }
        WinHttpHandle connect(WinHttpConnect(session.Get(), activeConfig.host.c_str(),
            activeConfig.port, 0));
        if (!connect) {
            NotifyRegistrationError(activeConfig.generation, "network_error", command);
            return;
        }

        const std::wstring path = JoinUrlPath(activeConfig.basePath,
            L"/api/devices/register");
        const DWORD flags = activeConfig.secure ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle request(WinHttpOpenRequest(connect.Get(), L"POST", path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!request || !WinHttpSetTimeouts(request.Get(), kNetworkTimeoutMs,
            kNetworkTimeoutMs, kNetworkTimeoutMs, kNetworkTimeoutMs)) {
            NotifyRegistrationError(activeConfig.generation, "network_error", command);
            return;
        }

        ActiveHandleScope requestScope(*this, request, activeConfig.generation);
        if (!requestScope) return;

        constexpr wchar_t headers[] = L"Content-Type: application/json\r\nAccept: application/json\r\n";
        if (ShouldAbort(activeConfig.generation)) return;
        bool requestSucceeded = WinHttpSendRequest(request.Get(), headers, static_cast<DWORD>(-1),
            requestBody.data(), static_cast<DWORD>(requestBody.size()),
            static_cast<DWORD>(requestBody.size()), 0) != FALSE;
        DWORD requestError = requestSucceeded ? ERROR_SUCCESS : GetLastError();
        SecureClear(requestBody);
        if (ShouldAbort(activeConfig.generation)) return;
        if (requestSucceeded) {
            if (ShouldAbort(activeConfig.generation)) return;
            requestSucceeded = WinHttpReceiveResponse(request.Get(), nullptr) != FALSE;
            if (!requestSucceeded) requestError = GetLastError();
            if (ShouldAbort(activeConfig.generation)) return;
        }
        if (!requestSucceeded) {
            if ((requestError == ERROR_WINHTTP_OPERATION_CANCELLED && ShouldStop()) ||
                ShouldAbort(activeConfig.generation)) {
                return;
            }
            NotifyRegistrationError(activeConfig.generation, "network_error", command);
            return;
        }

        DWORD httpStatus = 0;
        std::string responseBody;
        SecureClearGuard responseBodyGuard(responseBody);
        if (ShouldAbort(activeConfig.generation)) return;
        if (!QueryHttpStatus(request.Get(), httpStatus) ||
            ShouldAbort(activeConfig.generation) ||
            !ReadBoundedResponse(request.Get(), responseBody, activeConfig.generation)) {
            const bool aborted = ShouldAbort(activeConfig.generation);
            SecureClear(responseBody);
            if (!aborted) {
                NotifyRegistrationError(activeConfig.generation, "invalid_response", command);
            }
            return;
        }
        requestScope.Finish();
        if (ShouldAbort(activeConfig.generation)) return;
        if (httpStatus == HTTP_STATUS_CONFLICT) {
            NotifyRegistrationError(activeConfig.generation,
                "device_already_registered", command);
            SecureClear(responseBody);
            return;
        }
        if (httpStatus != HTTP_STATUS_CREATED) {
            NotifyRegistrationError(activeConfig.generation, "registration_failed", command);
            SecureClear(responseBody);
            return;
        }

        json response;
        try {
            response = json::parse(responseBody, nullptr, false);
        }
        catch (...) {
            response = json::value_t::discarded;
        }
        SecureClear(responseBody);
        if (response.is_discarded() || !response.is_object() ||
            !response.contains("deviceToken") || !response["deviceToken"].is_string()) {
            NotifyRegistrationError(activeConfig.generation, "invalid_response", command);
            return;
        }

        std::string token = response["deviceToken"].get<std::string>();
        SecureClearGuard tokenGuard(token);
        SecureClear(response["deviceToken"].get_ref<std::string&>());
        if (ShouldAbort(activeConfig.generation)) {
            response.clear();
            return;
        }
        json normalized = {
            { "type", "device_registered" },
            { "ok", true },
            { "deviceId", deviceId },
            { "deviceToken", token }
        };
        NotifyJson(activeConfig.generation, normalized, {}, true,
            &command.protectedResultReservation);
        SecureClear(normalized["deviceToken"].get_ref<std::string&>());
        normalized.clear();
        response.clear();
        SecureClear(token);
    }

    void NotifyRegistrationError(std::uint64_t generation, const std::string& code,
        Command& command)
    {
        NotifyJson(generation, json{
            { "type", "device_registration_error" },
            { "ok", false },
            { "code", code }
        }, {}, false, &command.protectedResultReservation);
    }

    bool OpenWebSocket(const Config& activeConfig, WebSocketConnection& connection)
    {
        connection.session.Reset(WinHttpOpen(L"DNFGameCapture CloudMatch/1",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0));
        if (!connection.session || !WinHttpSetTimeouts(connection.session.Get(),
            kNetworkTimeoutMs, kNetworkTimeoutMs, kNetworkTimeoutMs, kNetworkTimeoutMs)) {
            return false;
        }
        connection.connect.Reset(WinHttpConnect(connection.session.Get(),
            activeConfig.host.c_str(), activeConfig.port, 0));
        if (!connection.connect) return false;

        const std::wstring path = JoinUrlPath(activeConfig.basePath,
            L"/socket.io/?EIO=4&transport=websocket");
        const DWORD flags = activeConfig.secure ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle request(WinHttpOpenRequest(connection.connect.Get(), L"GET", path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!request || !WinHttpSetTimeouts(request.Get(), kNetworkTimeoutMs,
            kNetworkTimeoutMs, kNetworkTimeoutMs, kNetworkTimeoutMs)) {
            return false;
        }
        ActiveHandleScope requestScope(*this, request, activeConfig.generation);
        if (!requestScope) return false;
        if (ShouldAbort(activeConfig.generation) ||
            !WinHttpSetOption(request.Get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                nullptr, 0)) {
            return false;
        }
        if (ShouldAbort(activeConfig.generation) ||
            !WinHttpSendRequest(request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS,
                0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            ShouldAbort(activeConfig.generation)) {
            return false;
        }
        if (ShouldAbort(activeConfig.generation) ||
            !WinHttpReceiveResponse(request.Get(), nullptr) ||
            ShouldAbort(activeConfig.generation)) {
            return false;
        }

        DWORD httpStatus = 0;
        if (ShouldAbort(activeConfig.generation) ||
            !QueryHttpStatus(request.Get(), httpStatus) ||
            ShouldAbort(activeConfig.generation) ||
            httpStatus != HTTP_STATUS_SWITCH_PROTOCOLS) {
            return false;
        }
        connection.socket.Reset(WinHttpWebSocketCompleteUpgrade(request.Get(), 0));
        if (!connection.socket || ShouldAbort(activeConfig.generation)) return false;
        requestScope.Finish();

        DWORD receiveTimeout = kReceivePollTimeoutMs;
        DWORD sendTimeout = kNetworkTimeoutMs;
        if (!WinHttpSetOption(connection.socket.Get(), WINHTTP_OPTION_RECEIVE_TIMEOUT,
            &receiveTimeout, sizeof(receiveTimeout)) ||
            !WinHttpSetOption(connection.socket.Get(), WINHTTP_OPTION_SEND_TIMEOUT,
                &sendTimeout, sizeof(sendTimeout))) {
            return false;
        }
        connection.lastServerActivity = Clock::now();
        return true;
    }

    bool SendText(WebSocketConnection& connection, std::string_view message,
        std::uint64_t generation)
    {
        if (ShouldAbort(generation) || message.empty() ||
            message.size() > connection.outboundLimit ||
            message.size() >
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())) {
            return false;
        }
        ActiveHandleScope socketScope(*this, connection.socket, generation);
        if (!socketScope) return false;
        const DWORD error = WinHttpWebSocketSend(connection.socket.Get(),
            WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            const_cast<char*>(message.data()), static_cast<DWORD>(message.size()));
        if (ShouldAbort(generation)) return false;
        if (error == ERROR_WINHTTP_OPERATION_CANCELLED && ShouldStop()) return false;
        return error == NO_ERROR;
    }

    ReceiveResult ReceiveText(WebSocketConnection& connection, std::string& message,
        std::uint64_t generation)
    {
        if (ShouldAbort(generation)) return ReceiveResult::closed;
        std::array<unsigned char, 8192> buffer{};
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
        DWORD error = ERROR_SUCCESS;
        {
            ActiveHandleScope socketScope(*this, connection.socket, generation);
            if (!socketScope) {
                return ShouldAbort(generation) ? ReceiveResult::closed : ReceiveResult::failed;
            }
            error = WinHttpWebSocketReceive(connection.socket.Get(), buffer.data(),
                static_cast<DWORD>(buffer.size()), &bytesRead, &bufferType);
        }
        if (ShouldAbort(generation)) return ReceiveResult::closed;
        if (error == ERROR_WINHTTP_TIMEOUT) return ReceiveResult::poll;
        if (error == ERROR_WINHTTP_OPERATION_CANCELLED && ShouldStop()) {
            return ReceiveResult::closed;
        }
        if (ShouldStop()) return ReceiveResult::closed;
        if (error != NO_ERROR) return ReceiveResult::failed;
        if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return ReceiveResult::closed;

        connection.lastServerActivity = Clock::now();
        cloud_match::WebSocketBufferKind kind;
        switch (bufferType) {
        case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
            kind = cloud_match::WebSocketBufferKind::utf8Fragment;
            break;
        case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE:
            kind = cloud_match::WebSocketBufferKind::utf8Message;
            break;
        case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
            kind = cloud_match::WebSocketBufferKind::binaryFragment;
            break;
        case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE:
            kind = cloud_match::WebSocketBufferKind::binaryMessage;
            break;
        default:
            return ReceiveResult::rejected;
        }

        const auto assembled = connection.assembler.Add(
            std::string_view(reinterpret_cast<const char*>(buffer.data()), bytesRead),
            kind, message);
        if (assembled == cloud_match::WebSocketAssemblyResult::incomplete) {
            return ReceiveResult::poll;
        }
        return assembled == cloud_match::WebSocketAssemblyResult::complete ?
            ReceiveResult::message : ReceiveResult::rejected;
    }

    bool WaitForNamespaceConnection(const Config& activeConfig,
        WebSocketConnection& connection)
    {
        const auto deadline = Clock::now() + kAckTimeout;
        bool sentConnect = false;
        while (!ShouldAbort(activeConfig.generation) && Clock::now() < deadline) {
            std::string packet;
            SecureClearGuard packetGuard(packet);
            const ReceiveResult received = ReceiveText(connection, packet,
                activeConfig.generation);
            if (received == ReceiveResult::poll) continue;
            if (received == ReceiveResult::closed || received == ReceiveResult::failed) return false;
            if (received == ReceiveResult::rejected) {
                NotifyCloudError(activeConfig.generation, "binary_or_invalid_message");
                return false;
            }

            if (cloud_match::IsEngineIoPingPacket(packet)) {
                if (!SendText(connection, cloud_match::MakeEngineIoPongPacket(),
                    activeConfig.generation)) return false;
                continue;
            }

            cloud_match::EngineIoOpenPacket open;
            if (cloud_match::ParseEngineIoPacket(packet, open)) {
                connection.pingIntervalMs = open.pingIntervalMs;
                connection.pingTimeoutMs = open.pingTimeoutMs;
                if (open.maxPayload) {
                    connection.outboundLimit = static_cast<std::size_t>((std::min)(
                        *open.maxPayload,
                        static_cast<std::uint64_t>(cloud_match::kMaxCloudMatchPayloadBytes)));
                }
                UpdateOutboundLimit(activeConfig.generation, connection.outboundLimit);
                if (ShouldAbort(activeConfig.generation)) return false;
                std::string connectPacket = cloud_match::EncodeSocketIoConnectPacket(
                    activeConfig.deviceId, activeConfig.deviceToken, 1);
                const bool sent = SendText(connection, connectPacket,
                    activeConfig.generation);
                SecureClear(connectPacket);
                if (!sent) return false;
                sentConnect = true;
                continue;
            }

            cloud_match::SocketIoNamespaceConnected connected;
            if (sentConnect && cloud_match::ParseSocketIoNamespaceConnected(packet, connected)) {
                return true;
            }
            cloud_match::SocketIoConnectError connectError;
            if (cloud_match::ParseSocketIoConnectError(packet, connectError)) {
                json error = { { "code", connectError.code }, { "message", connectError.message } };
                NotifyCloudError(activeConfig.generation,
                    SanitizeServerCode(error, "code", "connect_error"),
                    SanitizeServerText(error, "message"));
                return false;
            }
            if (cloud_match::IsSocketIoDisconnectPacket(packet)) return false;
            NotifyCloudError(activeConfig.generation, "unknown_packet");
            return false;
        }
        return false;
    }

    bool SendQueuedCommand(const Config& activeConfig, WebSocketConnection& connection,
        Command& command)
    {
        json payload;
        std::string eventName;
        std::string resultType;
        switch (command.kind) {
        case CommandKind::joinRoom:
            eventName = "room:join";
            resultType = "room_join_result";
            payload = { { "roomId", command.argument },
                { "broadcasterName", command.secondArgument } };
            break;
        case CommandKind::renameRoom:
            eventName = "room:rename";
            resultType = "room_rename_result";
            payload = { { "broadcasterName", command.argument } };
            break;
        case CommandKind::leaveRoom:
            eventName = "room:leave";
            resultType = "room_leave_result";
            payload = json::object();
            break;
        case CommandKind::requestComparison:
            eventName = "room:comparison";
            resultType = "room_comparison_result";
            payload = json::object();
            break;
        case CommandKind::requestSnapshot:
            eventName = "snapshot:get";
            resultType = "snapshot_result";
            payload = { { "targetDeviceId", command.argument } };
            break;
        case CommandKind::uploadSnapshot:
        case CommandKind::registerDevice:
            return true;
        }
        return SendEventWithAck(activeConfig, connection, eventName, payload, command.kind,
            resultType, command.requestId, command.protectedResultReservation);
    }

    bool SendEventWithAck(const Config& activeConfig, WebSocketConnection& connection,
        const std::string& eventName, const json& payload, CommandKind kind,
        const std::string& resultType, const std::string& requestId,
        bool& protectedResultReservation)
    {
        std::uint64_t ackId = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != activeConfig.generation) return false;
            if (pendingAcks.size() >= kMaxPendingAcks) return true;
            if (nextAckId != (std::numeric_limits<std::uint64_t>::max)()) {
                ackId = nextAckId++;
            }
        }
        if (ackId == 0) {
            NotifyCloudError(activeConfig.generation, "ack_id_exhausted");
            return false;
        }
        std::string packet = cloud_match::EncodeSocketEvent(eventName, payload, ackId);
        const bool sent = !packet.empty() && SendText(connection, packet,
            activeConfig.generation);
        SecureClear(packet);
        if (!sent || ShouldAbort(activeConfig.generation)) return false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != activeConfig.generation) return false;
            pendingAcks.emplace(ackId, PendingAck{
                kind, resultType, requestId, 0, Clock::now() + kAckTimeout,
                protectedResultReservation
            });
            if (kind == CommandKind::joinRoom && desiredRoom &&
                desiredRoom->generation == activeConfig.generation &&
                desiredJoinPendingGeneration == activeConfig.generation) {
                desiredJoinInFlightGeneration = activeConfig.generation;
            }
            protectedResultReservation = false;
        }
        return true;
    }

    bool HasPendingAckCapacity(std::uint64_t generation) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return config.generation == generation && pendingAcks.size() < kMaxPendingAcks;
    }

    SnapshotSendResult SendSnapshotWithAck(const Config& activeConfig,
        WebSocketConnection& connection, PendingSnapshot& snapshot)
    {
        std::uint64_t ackId = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != activeConfig.generation) {
                return SnapshotSendResult::transportFailure;
            }
            if (nextAckId != (std::numeric_limits<std::uint64_t>::max)()) {
                ackId = nextAckId++;
            }
        }
        if (ackId == 0) {
            NotifyCloudError(activeConfig.generation, "ack_id_exhausted");
            return SnapshotSendResult::transportFailure;
        }

        std::string packet;
        const cloud_match::SnapshotUploadEncodeResult encoded =
            cloud_match::EncodeSnapshotUploadEvent(snapshot.jsonText, ackId,
                connection.outboundLimit, packet);
        if (encoded == cloud_match::SnapshotUploadEncodeResult::payloadTooLarge ||
            encoded == cloud_match::SnapshotUploadEncodeResult::invalidPayload) {
            SecureClear(packet);
            NotifySnapshotUploadFailure(activeConfig.generation,
                snapshot.clientRevision, encoded,
                &snapshot.protectedResultReservation);
            return SnapshotSendResult::rejected;
        }

        const bool sent = SendText(connection, packet, activeConfig.generation);
        SecureClear(packet);
        if (!sent || ShouldAbort(activeConfig.generation)) {
            return SnapshotSendResult::transportFailure;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != activeConfig.generation) {
                return SnapshotSendResult::transportFailure;
            }
            pendingAcks.emplace(ackId, PendingAck{
                CommandKind::uploadSnapshot, "snapshot_upload_result", {},
                snapshot.clientRevision, Clock::now() + kAckTimeout,
                snapshot.protectedResultReservation
            });
            snapshot.protectedResultReservation = false;
        }
        return SnapshotSendResult::sent;
    }

    bool ProcessOutgoing(const Config& activeConfig, WebSocketConnection& connection)
    {
        ProcessRegistrationCommands(activeConfig);
        if (ShouldAbort(activeConfig.generation)) return false;

        for (;;) {
            Command command;
            bool hasCommand = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                while (!commands.empty() && commands.front().generation != activeConfig.generation) {
                    commands.pop_front();
                }
                if (!commands.empty() && commands.front().kind != CommandKind::registerDevice &&
                    pendingAcks.size() < kMaxPendingAcks) {
                    command = std::move(commands.front());
                    commands.pop_front();
                    hasCommand = true;
                }
            }
            if (!hasCommand) break;
            if (ShouldAbort(activeConfig.generation)) return false;
            if (!SendQueuedCommand(activeConfig, connection, command)) {
                std::lock_guard<std::mutex> lock(mutex);
                if (config.generation == activeConfig.generation &&
                    command.protectedResultReservation) {
                    commands.push_front(std::move(command));
                }
                return false;
            }
            if (ShouldAbort(activeConfig.generation)) return false;
        }

        if (!HasPendingAckCapacity(activeConfig.generation)) return true;
        std::optional<PendingSnapshot> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (latestSnapshot && latestSnapshot->generation == activeConfig.generation) {
                snapshot = std::move(latestSnapshot);
                latestSnapshot.reset();
            }
        }
        if (!snapshot) return true;

        const SnapshotSendResult snapshotResult = SendSnapshotWithAck(activeConfig,
            connection, *snapshot);
        if (snapshotResult == SnapshotSendResult::transportFailure) {
            bool canceled = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!latestSnapshot && config.generation == activeConfig.generation) {
                    latestSnapshot = std::move(snapshot);
                }
                else if (config.generation == activeConfig.generation &&
                    snapshot->protectedResultReservation) {
                    canceled = true;
                }
            }
            if (canceled) NotifySnapshotCanceled(*snapshot);
            return false;
        }
        return true;
    }

    bool HasJoinCommandOrAckLocked(std::uint64_t generation) const
    {
        const bool queued = std::any_of(commands.begin(), commands.end(),
            [generation](const Command& command) {
                return command.generation == generation &&
                    command.kind == CommandKind::joinRoom;
            });
        if (queued) return true;
        return std::any_of(pendingAcks.begin(), pendingAcks.end(),
            [](const auto& entry) {
                return entry.second.kind == CommandKind::joinRoom;
            });
    }

    void MarkDesiredJoinPendingForConnection(std::uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (config.generation != generation || !desiredRoom ||
            desiredRoom->generation != generation) {
            return;
        }
        desiredJoinPendingGeneration = generation;
        desiredJoinInFlightGeneration.reset();
    }

    void SettleDesiredJoinLocked(std::uint64_t generation) noexcept
    {
        if (desiredJoinPendingGeneration == generation) {
            desiredJoinPendingGeneration.reset();
        }
        if (desiredJoinInFlightGeneration == generation) {
            desiredJoinInFlightGeneration.reset();
        }
    }

    RememberedJoinPreparation PrepareRememberedJoin(std::uint64_t generation,
        Command& join)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (config.generation != generation || !desiredRoom ||
            desiredRoom->generation != generation ||
            desiredJoinPendingGeneration != generation) {
            return RememberedJoinPreparation::noDesired;
        }
        if (desiredJoinInFlightGeneration == generation ||
            HasJoinCommandOrAckLocked(generation)) {
            return RememberedJoinPreparation::alreadyInFlight;
        }
        if (pendingAcks.size() >= kMaxPendingAcks ||
            !ReserveProtectedResultLocked(false)) {
            return RememberedJoinPreparation::noCapacity;
        }

        join.kind = CommandKind::joinRoom;
        join.argument = desiredRoom->roomId;
        join.secondArgument = desiredRoom->broadcasterName;
        join.generation = generation;
        join.protectedResultReservation = true;
        return RememberedJoinPreparation::ready;
    }

    void ReleaseRememberedJoinReservation(Command& join)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (config.generation == join.generation &&
            join.protectedResultReservation && protectedResultReservations > 0) {
            --protectedResultReservations;
            join.protectedResultReservation = false;
        }
    }

    RememberedJoinResult SendRememberedJoinIfNeeded(const Config& activeConfig,
        WebSocketConnection& connection)
    {
        Command join;
        switch (PrepareRememberedJoin(activeConfig.generation, join)) {
        case RememberedJoinPreparation::noDesired:
            return RememberedJoinResult::noDesired;
        case RememberedJoinPreparation::alreadyInFlight:
            return RememberedJoinResult::alreadyInFlight;
        case RememberedJoinPreparation::noCapacity:
            return RememberedJoinResult::noCapacity;
        case RememberedJoinPreparation::ready:
            break;
        }

        if (SendQueuedCommand(activeConfig, connection, join)) {
            return RememberedJoinResult::sent;
        }
        ReleaseRememberedJoinReservation(join);
        return RememberedJoinResult::networkFailure;
    }

    void HandleAck(const Config& activeConfig, const cloud_match::SocketIoAck& ack)
    {
        PendingAck pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != activeConfig.generation) return;
            const auto found = pendingAcks.find(ack.id);
            if (found == pendingAcks.end()) return;
            pending = std::move(found->second);
            pendingAcks.erase(found);
            if (pending.kind == CommandKind::joinRoom) {
                SettleDesiredJoinLocked(activeConfig.generation);
            }
        }

        json normalized;
        const bool validObject = ack.payload.is_object();
        const bool ok = validObject && ack.payload.value("ok", false);
        if (ok) {
            normalized = ack.payload;
            UpdateStatusFromAck(activeConfig.generation, pending.kind, ack.payload);
        }
        else {
            normalized = {
                { "ok", false },
                { "code", validObject ?
                    SanitizeServerCode(ack.payload, "code", "server_error") :
                    "invalid_response" }
            };
            if (validObject) {
                const std::string message = SanitizeServerText(ack.payload, "message");
                if (!message.empty()) normalized["message"] = message;
            }
        }
        normalized["type"] = pending.resultType;
        if (!pending.requestId.empty()) normalized["requestId"] = pending.requestId;
        if (pending.kind == CommandKind::uploadSnapshot) {
            normalized["clientRevision"] = pending.clientRevision;
        }
        NotifyJson(activeConfig.generation, normalized, {}, false,
            &pending.protectedResultReservation);
    }

    void UpdateStatusFromAck(std::uint64_t generation, CommandKind kind, const json& payload)
    {
        UpdateStatus(generation, [&](CloudMatchStatusSnapshot& current) {
            if (kind == CommandKind::joinRoom || kind == CommandKind::renameRoom) {
                const auto room = payload.find("room");
                if (room != payload.end() && room->is_object()) {
                    current.roomId = room->value("id", current.roomId);
                    current.roomName = room->value("displayName", current.roomName);
                }
                current.broadcasterName = payload.value("broadcasterName",
                    current.broadcasterName);
                current.roomRevision = payload.value("roomRevision", current.roomRevision);
                current.statusText = "in_room";
            }
            else if (kind == CommandKind::leaveRoom) {
                current.roomId.clear();
                current.roomName.clear();
                current.broadcasterName.clear();
                current.roomRevision = 0;
                current.statusText = "connected";
            }
            else if (payload.contains("roomRevision") &&
                payload["roomRevision"].is_number_unsigned()) {
                current.roomRevision = (std::max)(current.roomRevision,
                    payload["roomRevision"].get<std::uint64_t>());
            }
        });
    }

    void HandleServerEvent(const Config& activeConfig,
        const cloud_match::SocketIoEvent& event)
    {
        if (event.name == "room:changed" || event.name == "room:presence") {
            if (!event.payload.is_object()) {
                NotifyCloudError(activeConfig.generation, "invalid_event_payload");
                return;
            }
            const std::string type = event.name == "room:changed" ?
                "room_changed" : "room_presence";
            json normalized = event.payload;
            normalized["type"] = type;
            std::string coalesceKey = event.payload.value("roomId", std::string{});
            if (event.name == "room:presence") {
                coalesceKey += '\n';
                coalesceKey += event.payload.value("deviceId", std::string{});
            }
            if (event.payload.contains("roomRevision") &&
                event.payload["roomRevision"].is_number_unsigned()) {
                const std::uint64_t revision = event.payload["roomRevision"].get<std::uint64_t>();
                UpdateStatus(activeConfig.generation,
                    [revision](CloudMatchStatusSnapshot& current) {
                        current.roomRevision = (std::max)(current.roomRevision, revision);
                    });
            }
            NotifyJson(activeConfig.generation, normalized, std::move(coalesceKey));
            return;
        }
        if (event.name == "session:error") {
            if (event.payload.is_object()) {
                NotifyCloudError(activeConfig.generation,
                    SanitizeServerCode(event.payload, "code", "session_error"),
                    SanitizeServerText(event.payload, "message"));
            }
            else {
                NotifyCloudError(activeConfig.generation, "session_error");
            }
        }
    }

    void ExpireAcks(const Config& activeConfig)
    {
        const auto now = Clock::now();
        std::vector<PendingAck> expired;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != activeConfig.generation) return;
            for (auto iterator = pendingAcks.begin(); iterator != pendingAcks.end();) {
                if (iterator->second.deadline <= now) {
                    if (iterator->second.kind == CommandKind::joinRoom) {
                        SettleDesiredJoinLocked(activeConfig.generation);
                    }
                    expired.push_back(std::move(iterator->second));
                    iterator = pendingAcks.erase(iterator);
                }
                else {
                    ++iterator;
                }
            }
        }
        for (PendingAck& pending : expired) {
            json normalized = {
                { "type", pending.resultType },
                { "ok", false },
                { "code", "timeout" }
            };
            if (!pending.requestId.empty()) normalized["requestId"] = pending.requestId;
            if (pending.kind == CommandKind::uploadSnapshot) {
                normalized["clientRevision"] = pending.clientRevision;
            }
            NotifyJson(activeConfig.generation, normalized, {}, false,
                &pending.protectedResultReservation);
        }
    }

    void FailPendingAcks(const Config& activeConfig, const char* code)
    {
        std::unordered_map<std::uint64_t, PendingAck> failed;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (config.generation != activeConfig.generation) return;
            failed.swap(pendingAcks);
            if (desiredJoinInFlightGeneration == activeConfig.generation) {
                desiredJoinInFlightGeneration.reset();
                if (desiredRoom &&
                    desiredRoom->generation == activeConfig.generation) {
                    desiredJoinPendingGeneration = activeConfig.generation;
                }
            }
        }
        for (auto& entry : failed) {
            PendingAck& pending = entry.second;
            json normalized = {
                { "type", pending.resultType },
                { "ok", false },
                { "code", code }
            };
            if (!pending.requestId.empty()) normalized["requestId"] = pending.requestId;
            if (pending.kind == CommandKind::uploadSnapshot) {
                normalized["clientRevision"] = pending.clientRevision;
            }
            NotifyJson(activeConfig.generation, normalized, {}, false,
                &pending.protectedResultReservation);
        }
    }

    bool RunConnected(const Config& activeConfig, WebSocketConnection& connection)
    {
        MarkDesiredJoinPendingForConnection(activeConfig.generation);
        while (!ShouldAbort(activeConfig.generation)) {
            const RememberedJoinResult rememberedJoin =
                SendRememberedJoinIfNeeded(activeConfig, connection);
            if (rememberedJoin == RememberedJoinResult::networkFailure) return false;
            if (!ProcessOutgoing(activeConfig, connection)) return false;
            ExpireAcks(activeConfig);

            std::string packet;
            SecureClearGuard packetGuard(packet);
            const ReceiveResult received = ReceiveText(connection, packet,
                activeConfig.generation);
            if (received == ReceiveResult::closed || received == ReceiveResult::failed) return false;
            if (received == ReceiveResult::rejected) {
                NotifyCloudError(activeConfig.generation, "binary_or_invalid_message");
                return false;
            }
            if (received == ReceiveResult::message) {
                if (cloud_match::IsEngineIoPingPacket(packet)) {
                    if (!SendText(connection, cloud_match::MakeEngineIoPongPacket(),
                        activeConfig.generation)) return false;
                }
                else if (cloud_match::IsSocketIoDisconnectPacket(packet)) {
                    return false;
                }
                else {
                    cloud_match::SocketIoAck ack;
                    cloud_match::SocketIoEvent event;
                    if (cloud_match::ParseSocketIoAck(packet, ack)) {
                        HandleAck(activeConfig, ack);
                    }
                    else if (cloud_match::ParseSocketIoEvent(packet, event)) {
                        HandleServerEvent(activeConfig, event);
                    }
                    else {
                        NotifyCloudError(activeConfig.generation, "unknown_packet");
                        return false;
                    }
                }
            }

            const auto staleAfter = std::chrono::milliseconds(
                static_cast<std::uint64_t>(connection.pingIntervalMs) +
                connection.pingTimeoutMs);
            if (Clock::now() - connection.lastServerActivity > staleAfter) {
                NotifyCloudError(activeConfig.generation, "connection_stale");
                return false;
            }
        }
        return false;
    }

    bool RunConnection(const Config& activeConfig, bool& connectedOnce)
    {
        WebSocketConnection connection;
        if (!OpenWebSocket(activeConfig, connection) || ShouldAbort(activeConfig.generation)) {
            return false;
        }
        if (!WaitForNamespaceConnection(activeConfig, connection) ||
            ShouldAbort(activeConfig.generation)) {
            if (connection.socket) {
                WinHttpWebSocketClose(connection.socket.Get(),
                    WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            }
            return false;
        }

        connectedOnce = true;
        UpdateStatus(activeConfig.generation, [](CloudMatchStatusSnapshot& current) {
            current.connecting = false;
            current.connected = true;
            current.reconnecting = false;
            current.statusText = "connected";
        });
        const bool result = RunConnected(activeConfig, connection);
        if (connection.socket && !ShouldStop()) {
            WinHttpWebSocketClose(connection.socket.Get(),
                WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        }
        return result;
    }

    bool WaitForWorkerChange(std::uint64_t generation,
        std::chrono::milliseconds duration)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, duration, [&]() {
            return stopRequested || config.generation != generation ||
                HasRegistrationCommand(generation);
        });
    }

    void WorkerMain()
    {
        std::size_t reconnectIndex = 0;
        bool reconnecting = false;
        std::uint64_t observedGeneration = (std::numeric_limits<std::uint64_t>::max)();
        for (;;) {
            Config activeConfig = CopyConfig();
            if (ShouldStop()) break;
            if (configGeneration.load(std::memory_order_acquire) != activeConfig.generation) {
                continue;
            }
            if (observedGeneration != activeConfig.generation) {
                observedGeneration = activeConfig.generation;
                reconnectIndex = 0;
                reconnecting = false;
            }

            ProcessRegistrationCommands(activeConfig);
            if (ShouldStop()) break;
            if (ShouldAbort(activeConfig.generation)) continue;
            activeConfig = CopyConfig();
            if (ShouldStop()) break;
            if (ShouldAbort(activeConfig.generation)) continue;
            if (!activeConfig.serverValid || !activeConfig.credentialsValid) {
                UpdateStatus(activeConfig.generation, [](CloudMatchStatusSnapshot& current) {
                    current.connecting = false;
                    current.connected = false;
                    current.reconnecting = false;
                });
                WaitForWorkerChange(activeConfig.generation, std::chrono::hours(24));
                continue;
            }

            UpdateStatus(activeConfig.generation,
                [reconnecting](CloudMatchStatusSnapshot& current) {
                    current.connecting = !reconnecting;
                    current.connected = false;
                    current.reconnecting = reconnecting;
                    current.statusText = reconnecting ? "reconnecting" : "connecting";
                });

            bool connectedOnce = false;
            RunConnection(activeConfig, connectedOnce);
            if (ShouldStop()) break;
            if (ShouldAbort(activeConfig.generation)) {
                std::lock_guard<std::mutex> lock(mutex);
                ClearPendingAcksLocked();
                continue;
            }
            FailPendingAcks(activeConfig, "connection_lost");

            UpdateStatus(activeConfig.generation, [](CloudMatchStatusSnapshot& current) {
                current.connecting = false;
                current.connected = false;
                current.reconnecting = true;
                current.statusText = "reconnecting";
            });
            reconnecting = true;
            if (connectedOnce) reconnectIndex = 0;
            const int delaySeconds = kReconnectDelaysSeconds[
                (std::min)(reconnectIndex, kReconnectDelaysSeconds.size() - 1)];
            if (reconnectIndex + 1 < kReconnectDelaysSeconds.size()) ++reconnectIndex;
            WaitForWorkerChange(activeConfig.generation,
                std::chrono::seconds(delaySeconds));
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            ClearPendingAcksLocked();
        }
        const std::uint64_t finalGeneration = configGeneration.load();
        UpdateStatus(finalGeneration, [](CloudMatchStatusSnapshot& current) {
            current.connecting = false;
            current.connected = false;
            current.reconnecting = false;
        });
    }

    mutable std::recursive_mutex dispatchGate;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
    bool started = false;
    bool stopping = false;
    std::atomic<bool> stopRequested{ false };
    std::atomic<HINTERNET> activeCancelableHandle{ nullptr };

    Config config;
    std::atomic<std::uint64_t> configGeneration{ 0 };
    std::deque<Command> commands;
    std::optional<PendingSnapshot> latestSnapshot;
    std::optional<DesiredRoom> desiredRoom;
    std::optional<std::uint64_t> desiredJoinPendingGeneration;
    std::optional<std::uint64_t> desiredJoinInFlightGeneration;
    std::uint64_t nextCommandSequence = 1;
    std::size_t currentOutboundLimit = cloud_match::kMaxCloudMatchPayloadBytes;

    CloudMatchStatusSnapshot status;
    MessageCallback callback;
    std::deque<InboundMessage> inboundMessages;
    std::size_t protectedResultsQueued = 0;
    std::size_t protectedResultReservations = 0;

    std::unordered_map<std::uint64_t, PendingAck> pendingAcks;
    std::uint64_t nextAckId = 1;
#ifdef CLOUD_MATCH_CLIENT_STANDALONE
    std::size_t rememberedJoinSendCountForTesting = 0;
#endif
};

CloudMatchClient::CloudMatchClient() : impl_(std::make_unique<Impl>()) {}

CloudMatchClient::~CloudMatchClient()
{
    Stop();
}

bool CloudMatchClient::Configure(const std::string& serverUrl,
    const std::string& deviceId, const std::string& deviceToken)
{
    std::wstring wideUrl = Utf8ToWide(serverUrl);
    const bool configured = wideUrl.empty() ? impl_->Configure({}, deviceId, deviceToken) :
        impl_->Configure(wideUrl, deviceId, deviceToken);
    SecureClear(wideUrl);
    return configured;
}

bool CloudMatchClient::Configure(const std::wstring& serverUrl,
    const std::string& deviceId, const std::string& deviceToken)
{
    return impl_->Configure(serverUrl, deviceId, deviceToken);
}

bool CloudMatchClient::Start()
{
    return impl_->Start();
}

void CloudMatchClient::Stop()
{
    if (impl_) impl_->Stop();
}

bool CloudMatchClient::RegisterDevice(const std::string& deviceId)
{
    return impl_->RegisterDevice(deviceId);
}

bool CloudMatchClient::JoinRoom(const std::string& roomId,
    const std::string& broadcasterName)
{
    return impl_->JoinRoom(roomId, broadcasterName);
}

bool CloudMatchClient::Rename(const std::string& broadcasterName)
{
    return impl_->Rename(broadcasterName);
}

bool CloudMatchClient::LeaveRoom()
{
    return impl_->LeaveRoom();
}

bool CloudMatchClient::UploadSnapshot(std::string snapshotJson)
{
    return impl_->UploadSnapshot(std::move(snapshotJson));
}

bool CloudMatchClient::RequestComparison(const std::string& requestId)
{
    return impl_->RequestComparison(requestId);
}

bool CloudMatchClient::RequestComparison(unsigned int requestId)
{
    return RequestComparison(std::to_string(requestId));
}

bool CloudMatchClient::RequestSnapshot(const std::string& requestId,
    const std::string& targetDeviceId)
{
    return impl_->RequestSnapshot(requestId, targetDeviceId);
}

bool CloudMatchClient::RequestSnapshot(unsigned int requestId,
    const std::string& targetDeviceId)
{
    return RequestSnapshot(std::to_string(requestId), targetDeviceId);
}

CloudMatchStatusSnapshot CloudMatchClient::GetStatusSnapshot() const
{
    return impl_->GetStatusSnapshot();
}

void CloudMatchClient::SetMessageCallback(MessageCallback callback)
{
    impl_->SetMessageCallback(std::move(callback));
}

std::size_t CloudMatchClient::DispatchMessages(std::size_t maxCount)
{
    return impl_->DispatchMessages(maxCount);
}

#ifdef CLOUD_MATCH_CLIENT_STANDALONE
std::uint64_t CloudMatchClient::ConfigureForTesting()
{
    return impl_->ConfigureForTesting();
}

bool CloudMatchClient::CompleteNextProtectedOperationForTesting()
{
    return impl_->CompleteNextProtectedOperationForTesting();
}

bool CloudMatchClient::CompleteLatestSnapshotAckForTesting(bool ok,
    std::uint64_t acceptedRevision, const std::string& code)
{
    return impl_->CompleteLatestSnapshotAckForTesting(ok, acceptedRevision, code);
}

bool CloudMatchClient::JoinRoomForGenerationForTesting(std::uint64_t generation,
    const std::string& roomId, const std::string& broadcasterName)
{
    return impl_->JoinRoomForGenerationForTesting(generation, roomId, broadcasterName);
}

bool CloudMatchClient::HasDesiredRoomForTesting() const
{
    return impl_->HasDesiredRoomForTesting();
}

std::uint64_t CloudMatchClient::DesiredRoomGenerationForTesting() const
{
    return impl_->DesiredRoomGenerationForTesting();
}

void CloudMatchClient::SetDesiredJoinForReplayForTesting(const std::string& roomId,
    const std::string& broadcasterName)
{
    impl_->SetDesiredJoinForReplayForTesting(roomId, broadcasterName);
}

std::string CloudMatchClient::RetryRememberedJoinForTesting()
{
    return impl_->RetryRememberedJoinForTesting();
}

std::size_t CloudMatchClient::RememberedJoinSendCountForTesting() const
{
    return impl_->RememberedJoinSendCountForTesting();
}
#endif
