#pragma once

#include "json.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cloud_match {

constexpr std::size_t kMaxCloudMatchPayloadBytes = 65536;

struct EngineIoOpenPacket
{
    std::string sid;
    std::uint32_t pingIntervalMs = 0;
    std::uint32_t pingTimeoutMs = 0;
    std::optional<std::uint64_t> maxPayload;
};

struct SocketIoNamespaceConnected
{
    std::string sid;
};

struct SocketIoConnectError
{
    std::string message;
    std::string code;
};

struct SocketIoEvent
{
    std::string name;
    nlohmann::json payload;
};

struct SocketIoAck
{
    std::uint64_t id = 0;
    nlohmann::json payload;
};

bool ParseEngineIoPacket(std::string_view packet, EngineIoOpenPacket& result) noexcept;
bool IsEngineIoPingPacket(std::string_view packet) noexcept;
std::string MakeEngineIoPongPacket();

std::string EncodeSocketIoConnectPacket(std::string_view deviceId,
    std::string_view deviceToken, int protocolVersion) noexcept;
bool ParseSocketIoNamespaceConnected(std::string_view packet,
    SocketIoNamespaceConnected& result) noexcept;
bool ParseSocketIoConnectError(std::string_view packet,
    SocketIoConnectError& result) noexcept;

std::string EncodeSocketEvent(std::string_view eventName,
    const nlohmann::json& payload) noexcept;
std::string EncodeSocketEvent(std::string_view eventName,
    const nlohmann::json& payload, std::uint64_t ackId) noexcept;

enum class SnapshotUploadEncodeResult
{
    success,
    invalidPayload,
    payloadTooLarge
};

SnapshotUploadEncodeResult EncodeSnapshotUploadEvent(std::string_view snapshotJson,
    std::uint64_t ackId, std::size_t maxEventBytes, std::string& encodedPacket) noexcept;
bool ParseSocketIoEvent(std::string_view packet, SocketIoEvent& result) noexcept;
bool ParseSocketIoAck(std::string_view packet, SocketIoAck& result) noexcept;
bool IsSocketIoDisconnectPacket(std::string_view packet) noexcept;

enum class WebSocketBufferKind
{
    utf8Fragment,
    utf8Message,
    binaryFragment,
    binaryMessage
};

enum class WebSocketAssemblyResult
{
    incomplete,
    complete,
    rejected
};

class WebSocketTextAssembler
{
public:
    WebSocketAssemblyResult Add(std::string_view bytes, WebSocketBufferKind kind,
        std::string& completedMessage) noexcept;
    void Reset() noexcept;

private:
    std::string buffer_;
};

} // namespace cloud_match
