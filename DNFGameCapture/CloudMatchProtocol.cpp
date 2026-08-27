#ifndef CLOUD_MATCH_PROTOCOL_STANDALONE
#include "pch.h"
#endif

#include "CloudMatchProtocol.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <utility>

namespace cloud_match {
namespace {

using nlohmann::json;

void SecureWipeString(std::string& value) noexcept
{
    volatile char* bytes = value.empty() ? nullptr : value.data();
    for (std::size_t index = 0; index < value.size(); ++index) {
        bytes[index] = '\0';
    }
    value.clear();
}

void SecureWipeJsonString(json& value, const char* key) noexcept
{
    try {
        const auto found = value.find(key);
        if (found != value.end() && found->is_string()) {
            SecureWipeString(found->get_ref<std::string&>());
        }
    }
    catch (...) {
    }
}

bool IsValidUtf8(std::string_view text) noexcept
{
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }

        std::size_t length = 0;
        unsigned char secondMinimum = 0x80;
        unsigned char secondMaximum = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            length = 2;
        }
        else if (first >= 0xe0 && first <= 0xef) {
            length = 3;
            if (first == 0xe0) secondMinimum = 0xa0;
            if (first == 0xed) secondMaximum = 0x9f;
        }
        else if (first >= 0xf0 && first <= 0xf4) {
            length = 4;
            if (first == 0xf0) secondMinimum = 0x90;
            if (first == 0xf4) secondMaximum = 0x8f;
        }
        else {
            return false;
        }

        if (index + length > text.size()) return false;
        const auto second = static_cast<unsigned char>(text[index + 1]);
        if (second < secondMinimum || second > secondMaximum) return false;
        for (std::size_t continuation = 2; continuation < length; ++continuation) {
            const auto value = static_cast<unsigned char>(text[index + continuation]);
            if (value < 0x80 || value > 0xbf) return false;
        }
        index += length;
    }
    return true;
}

bool IsPacketValid(std::string_view packet) noexcept
{
    return packet.size() <= kMaxCloudMatchInboundPayloadBytes && IsValidUtf8(packet);
}

bool ParseJsonAfterPrefix(std::string_view packet, std::string_view prefix,
    json& value) noexcept
{
    if (!IsPacketValid(packet) || packet.size() <= prefix.size() ||
        packet.substr(0, prefix.size()) != prefix) {
        return false;
    }
    try {
        value = json::parse(packet.begin() + prefix.size(), packet.end(), nullptr, false);
        return !value.is_discarded();
    }
    catch (...) {
        return false;
    }
}

bool ReadPositiveUint32(const json& object, const char* key, std::uint32_t& value) noexcept
{
    try {
        const auto found = object.find(key);
        if (found == object.end() ||
            (!found->is_number_unsigned() && !found->is_number_integer())) {
            return false;
        }
        const auto parsed = found->get<std::int64_t>();
        if (parsed <= 0 || parsed > (std::numeric_limits<std::uint32_t>::max)()) return false;
        value = static_cast<std::uint32_t>(parsed);
        return true;
    }
    catch (...) {
        return false;
    }
}

std::string EncodeWithPrefix(std::string_view prefix, const json& value) noexcept
{
    try {
        std::string result(prefix);
        result += value.dump();
        if (!IsPacketValid(result)) return {};
        return result;
    }
    catch (...) {
        return {};
    }
}

} // namespace

bool ParseEngineIoPacket(std::string_view packet, EngineIoOpenPacket& result) noexcept
{
    json value;
    if (!ParseJsonAfterPrefix(packet, "0", value) || !value.is_object()) return false;
    try {
        const auto sid = value.find("sid");
        if (sid == value.end() || !sid->is_string() || sid->get_ref<const std::string&>().empty()) {
            return false;
        }

        EngineIoOpenPacket parsed;
        parsed.sid = sid->get<std::string>();
        if (!ReadPositiveUint32(value, "pingInterval", parsed.pingIntervalMs) ||
            !ReadPositiveUint32(value, "pingTimeout", parsed.pingTimeoutMs)) {
            return false;
        }

        const auto maxPayload = value.find("maxPayload");
        if (maxPayload != value.end()) {
            if ((!maxPayload->is_number_unsigned() && !maxPayload->is_number_integer()) ||
                maxPayload->is_number_float()) {
                return false;
            }
            if (maxPayload->is_number_integer() && maxPayload->get<std::int64_t>() <= 0) {
                return false;
            }
            const auto parsedMaximum = maxPayload->get<std::uint64_t>();
            if (parsedMaximum == 0) return false;
            parsed.maxPayload = parsedMaximum;
        }
        result = std::move(parsed);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool IsEngineIoPingPacket(std::string_view packet) noexcept
{
    return packet == "2";
}

std::string MakeEngineIoPongPacket()
{
    return "3";
}

std::string EncodeSocketIoConnectPacket(std::string_view deviceId,
    std::string_view deviceToken, int protocolVersion) noexcept
{
    if (deviceId.empty() || deviceToken.empty() || protocolVersion <= 0) return {};
    json auth;
    try {
        auth = json{
            { "deviceId", std::string(deviceId) },
            { "deviceToken", std::string(deviceToken) },
            { "protocolVersion", protocolVersion }
        };
        std::string packet = EncodeWithPrefix("40", auth);
        SecureWipeJsonString(auth, "deviceToken");
        auth.clear();
        return packet;
    }
    catch (...) {
        SecureWipeJsonString(auth, "deviceToken");
        auth.clear();
        return {};
    }
}

bool ParseSocketIoNamespaceConnected(std::string_view packet,
    SocketIoNamespaceConnected& result) noexcept
{
    json value;
    if (!ParseJsonAfterPrefix(packet, "40", value) || !value.is_object()) return false;
    try {
        const auto sid = value.find("sid");
        if (sid == value.end() || !sid->is_string() || sid->get_ref<const std::string&>().empty()) {
            return false;
        }
        SocketIoNamespaceConnected parsed;
        parsed.sid = sid->get<std::string>();
        result = std::move(parsed);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool ParseSocketIoConnectError(std::string_view packet,
    SocketIoConnectError& result) noexcept
{
    json value;
    if (!ParseJsonAfterPrefix(packet, "44", value) || !value.is_object()) return false;
    try {
        const auto message = value.find("message");
        const auto data = value.find("data");
        if (message == value.end() || !message->is_string() ||
            data == value.end() || !data->is_object()) {
            return false;
        }
        const auto code = data->find("code");
        if (code == data->end() || !code->is_string()) return false;

        SocketIoConnectError parsed;
        parsed.message = message->get<std::string>();
        parsed.code = code->get<std::string>();
        result = std::move(parsed);
        return true;
    }
    catch (...) {
        return false;
    }
}

std::string EncodeSocketEvent(std::string_view eventName,
    const nlohmann::json& payload) noexcept
{
    if (eventName.empty()) return {};
    try {
        return EncodeWithPrefix("42", json::array({ std::string(eventName), payload }));
    }
    catch (...) {
        return {};
    }
}

std::string EncodeSocketEvent(std::string_view eventName,
    const nlohmann::json& payload, std::uint64_t ackId) noexcept
{
    if (eventName.empty()) return {};
    try {
        const json event = json::array({ std::string(eventName), payload });
        return EncodeWithPrefix("42" + std::to_string(ackId), event);
    }
    catch (...) {
        return {};
    }
}

SnapshotUploadEncodeResult EncodeSnapshotUploadEvent(std::string_view snapshotJson,
    std::uint64_t ackId, std::size_t maxEventBytes, std::string& encodedPacket) noexcept
{
    encodedPacket.clear();
    const std::size_t effectiveLimit = (std::min)(maxEventBytes,
        kMaxCloudMatchPayloadBytes);
    if (snapshotJson.empty() || snapshotJson.size() > effectiveLimit) {
        return snapshotJson.empty() ? SnapshotUploadEncodeResult::invalidPayload :
            SnapshotUploadEncodeResult::payloadTooLarge;
    }
    if (!IsValidUtf8(snapshotJson)) return SnapshotUploadEncodeResult::invalidPayload;

    try {
        json snapshot = json::parse(snapshotJson.begin(), snapshotJson.end(), nullptr, false);
        if (snapshot.is_discarded() || !snapshot.is_object()) {
            return SnapshotUploadEncodeResult::invalidPayload;
        }

        std::string packet = "42" + std::to_string(ackId);
        packet += json::array({ "snapshot:upload", json{ { "snapshot", std::move(snapshot) } } }).dump();
        if (packet.size() > effectiveLimit) {
            return SnapshotUploadEncodeResult::payloadTooLarge;
        }
        encodedPacket = std::move(packet);
        return SnapshotUploadEncodeResult::success;
    }
    catch (...) {
        encodedPacket.clear();
        return SnapshotUploadEncodeResult::invalidPayload;
    }
}

bool ParseSocketIoEvent(std::string_view packet, SocketIoEvent& result) noexcept
{
    json value;
    if (!ParseJsonAfterPrefix(packet, "42", value) || !value.is_array() || value.size() != 2 ||
        !value[0].is_string() || value[0].get_ref<const std::string&>().empty()) {
        return false;
    }
    try {
        SocketIoEvent parsed;
        parsed.name = value[0].get<std::string>();
        parsed.payload = value[1];
        result = std::move(parsed);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool ParseSocketIoAck(std::string_view packet, SocketIoAck& result) noexcept
{
    if (!IsPacketValid(packet) || packet.size() < 5 || packet.substr(0, 2) != "43") {
        return false;
    }
    const std::size_t payloadStart = packet.find('[', 2);
    if (payloadStart == std::string_view::npos || payloadStart == 2) return false;

    std::uint64_t ackId = 0;
    const char* idFirst = packet.data() + 2;
    const char* idLast = packet.data() + payloadStart;
    const auto converted = std::from_chars(idFirst, idLast, ackId);
    if (converted.ec != std::errc() || converted.ptr != idLast) return false;

    try {
        json payloads = json::parse(packet.begin() + payloadStart, packet.end(), nullptr, false);
        if (payloads.is_discarded() || !payloads.is_array() || payloads.size() != 1) return false;
        SocketIoAck parsed;
        parsed.id = ackId;
        parsed.payload = std::move(payloads[0]);
        result = std::move(parsed);
        return true;
    }
    catch (...) {
        return false;
    }
}

bool IsSocketIoDisconnectPacket(std::string_view packet) noexcept
{
    return packet == "41";
}

WebSocketAssemblyResult WebSocketTextAssembler::Add(std::string_view bytes,
    WebSocketBufferKind kind, std::string& completedMessage) noexcept
{
    completedMessage.clear();
    if (kind == WebSocketBufferKind::binaryFragment ||
        kind == WebSocketBufferKind::binaryMessage) {
        Reset();
        return WebSocketAssemblyResult::rejected;
    }
    if (bytes.size() > kMaxCloudMatchInboundPayloadBytes - buffer_.size()) {
        Reset();
        return WebSocketAssemblyResult::rejected;
    }

    try {
        buffer_.append(bytes.data(), bytes.size());
        if (kind == WebSocketBufferKind::utf8Fragment) {
            return WebSocketAssemblyResult::incomplete;
        }
        if (!IsValidUtf8(buffer_)) {
            Reset();
            return WebSocketAssemblyResult::rejected;
        }
        completedMessage = std::move(buffer_);
        buffer_.clear();
        return WebSocketAssemblyResult::complete;
    }
    catch (...) {
        Reset();
        completedMessage.clear();
        return WebSocketAssemblyResult::rejected;
    }
}

void WebSocketTextAssembler::Reset() noexcept
{
    buffer_.clear();
}

} // namespace cloud_match
