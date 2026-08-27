#include "../CloudMatchProtocol.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using nlohmann::json;
using namespace cloud_match;

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void TestEngineIoPackets()
{
    EngineIoOpenPacket open;
    Require(ParseEngineIoPacket(
        R"(0{"sid":"abc","pingInterval":25000,"pingTimeout":20000})", open),
        "Engine.IO open packet should parse");
    Require(open.sid == "abc", "Engine.IO sid should be exact UTF-8");
    Require(open.pingIntervalMs == 25000, "Engine.IO ping interval should parse");
    Require(open.pingTimeoutMs == 20000, "Engine.IO ping timeout should parse");
    Require(!open.maxPayload.has_value(), "Engine.IO maxPayload should be optional");

    Require(ParseEngineIoPacket(
        R"(0{"sid":"room-sid","pingInterval":1000,"pingTimeout":2000,"maxPayload":65536})",
        open), "Engine.IO maxPayload should parse");
    Require(open.maxPayload == 65536, "Engine.IO maxPayload should retain its value");
    Require(IsEngineIoPingPacket("2"), "Engine.IO ping should be recognized");
    Require(!IsEngineIoPingPacket("2probe"), "Only the exact ping packet is supported");
    Require(MakeEngineIoPongPacket() == "3", "Engine.IO pong should be exact");
}

void TestSocketIoPackets()
{
    const std::string connect = EncodeSocketIoConnectPacket("device-a", "token-b", 1);
    Require(connect.rfind("40", 0) == 0, "Socket.IO connect prefix should be exact");
    const json connectAuth = json::parse(connect.substr(2));
    Require(connectAuth == json({
        { "deviceId", "device-a" },
        { "deviceToken", "token-b" },
        { "protocolVersion", 1 }
    }), "Socket.IO connect auth should preserve semantic JSON");

    SocketIoNamespaceConnected connected;
    Require(ParseSocketIoNamespaceConnected(R"(40{"sid":"namespace-sid"})", connected),
        "Socket.IO namespace connected packet should parse");
    Require(connected.sid == "namespace-sid", "namespace sid should parse");

    SocketIoConnectError connectError;
    Require(ParseSocketIoConnectError(
        R"(44{"message":"denied","data":{"code":"invalid_token"}})", connectError),
        "Socket.IO connect error should parse");
    Require(connectError.message == "denied", "connect error message should parse");
    Require(connectError.code == "invalid_token", "connect error code should parse");

    Require(EncodeSocketEvent("room:list", json::object()) ==
        R"(42["room:list",{}])", "Socket.IO event encoding without an ACK should be exact");
    Require(EncodeSocketEvent("room:list", json::object(), 31) ==
        R"(4231["room:list",{}])", "Socket.IO event encoding should include ack id");

    SocketIoAck ack;
    Require(ParseSocketIoAck(R"(431[{"ok":true}])", ack),
        "Socket.IO ACK id 1 should parse without ambiguity");
    Require(ack.id == 1, "single-digit Socket.IO ACK id should parse");
    Require(ack.payload == json({ { "ok", true } }),
        "single-digit Socket.IO ACK payload should parse");

    Require(ParseSocketIoAck(R"(4331[{"ok":true}])", ack),
        "Socket.IO ACK should parse");
    Require(ack.id == 31, "Socket.IO ACK id should parse");
    Require(ack.payload == json({ { "ok", true } }), "Socket.IO ACK payload should parse");

    SocketIoEvent event;
    Require(ParseSocketIoEvent(
        R"(42["room:changed",{"roomId":"room-1","revision":7}])", event),
        "Socket.IO event should parse");
    Require(event.name == "room:changed", "Socket.IO event name should parse");
    Require(event.payload["roomId"] == "room-1", "Socket.IO event payload should parse");
    Require(IsSocketIoDisconnectPacket("41"), "Socket.IO disconnect should be recognized");
    Require(!IsSocketIoDisconnectPacket("410"), "Socket.IO disconnect must be exact");
}

void TestSnapshotUploadEnvelopeLimits()
{
    constexpr std::uint64_t sizingAckId =
        (std::numeric_limits<std::uint64_t>::max)() - 1;
    const std::string emptySnapshot = R"({"padding":""})";
    std::string encoded;
    Require(EncodeSnapshotUploadEvent(emptySnapshot, sizingAckId,
        kMaxCloudMatchPayloadBytes, encoded) == SnapshotUploadEncodeResult::success,
        "small snapshot envelope should encode");
    Require(encoded.size() < kMaxCloudMatchPayloadBytes,
        "small snapshot should leave room in the transport envelope");

    const std::size_t paddingBytes = kMaxCloudMatchPayloadBytes - encoded.size();
    const std::string largestSnapshot = std::string(R"({"padding":")") +
        std::string(paddingBytes, 'x') + R"("})";
    Require(EncodeSnapshotUploadEvent(largestSnapshot, sizingAckId,
        kMaxCloudMatchPayloadBytes, encoded) == SnapshotUploadEncodeResult::success,
        "largest fully encoded snapshot event should be accepted");
    Require(encoded.size() == kMaxCloudMatchPayloadBytes,
        "largest accepted snapshot event should exactly fill the local limit");

    const std::string oneByteOver = std::string(R"({"padding":")") +
        std::string(paddingBytes + 1, 'x') + R"("})";
    Require(EncodeSnapshotUploadEvent(oneByteOver, sizingAckId,
        kMaxCloudMatchPayloadBytes, encoded) == SnapshotUploadEncodeResult::payloadTooLarge,
        "one-byte-over encoded snapshot event should be rejected");
    Require(encoded.empty(), "rejected snapshot event must not retain encoded bytes");

    Require(EncodeSnapshotUploadEvent(largestSnapshot, sizingAckId, 1024, encoded) ==
        SnapshotUploadEncodeResult::payloadTooLarge,
        "smaller advertised Engine.IO maxPayload should reject the same snapshot");
    Require(encoded.empty(), "server-limit rejection must not retain encoded bytes");

    const std::string invalidUtf8 = std::string(R"({"padding":")") +
        std::string("\xC3\x28", 2) + R"("})";
    Require(EncodeSnapshotUploadEvent(invalidUtf8, sizingAckId,
        kMaxCloudMatchPayloadBytes, encoded) == SnapshotUploadEncodeResult::invalidPayload,
        "invalid UTF-8 snapshot should be rejected before transport");
}

void TestFragmentAssembly()
{
    WebSocketTextAssembler assembler;
    std::string message;
    Require(assembler.Add("42[\"room:", WebSocketBufferKind::utf8Fragment, message) ==
        WebSocketAssemblyResult::incomplete, "first UTF-8 fragment should be incomplete");
    Require(assembler.Add("changed\",{\"revision\":9}]", WebSocketBufferKind::utf8Message,
        message) == WebSocketAssemblyResult::complete, "final UTF-8 fragment should complete");
    Require(message == R"(42["room:changed",{"revision":9}])",
        "fragment assembly should preserve exact bytes");

    const std::string splitUtf8 = std::string("42[\"room:changed\",{\"name\":\"") +
        "\xE6\x88\xBF" + "\"}]";
    const std::size_t splitAt = splitUtf8.find("\xE6") + 1;
    Require(assembler.Add(std::string_view(splitUtf8).substr(0, splitAt),
        WebSocketBufferKind::utf8Fragment, message) == WebSocketAssemblyResult::incomplete,
        "UTF-8 code point may span WebSocket fragments");
    Require(assembler.Add(std::string_view(splitUtf8).substr(splitAt),
        WebSocketBufferKind::utf8Message, message) == WebSocketAssemblyResult::complete,
        "split UTF-8 code point should validate after assembly");
    Require(message == splitUtf8, "assembled UTF-8 should remain exact");

    Require(assembler.Add("binary", WebSocketBufferKind::binaryMessage, message) ==
        WebSocketAssemblyResult::rejected, "binary messages should be rejected");
    Require(assembler.Add("binary", WebSocketBufferKind::binaryFragment, message) ==
        WebSocketAssemblyResult::rejected, "binary fragments should be rejected");

    const std::string maximum(kMaxCloudMatchPayloadBytes, 'x');
    Require(assembler.Add(maximum, WebSocketBufferKind::utf8Message, message) ==
        WebSocketAssemblyResult::complete, "65536-byte payload should be accepted");
    Require(message.size() == kMaxCloudMatchPayloadBytes,
        "maximum payload should not be truncated");

    const std::string responseEnvelope(kMaxCloudMatchPayloadBytes + 1024, 'x');
    Require(assembler.Add(responseEnvelope, WebSocketBufferKind::utf8Message, message) ==
        WebSocketAssemblyResult::complete,
        "response envelope above the snapshot limit should be accepted");
    const std::string oversizedInbound(131073, 'x');
    Require(assembler.Add(oversizedInbound, WebSocketBufferKind::utf8Message, message) ==
        WebSocketAssemblyResult::rejected, "payload above the inbound limit should fail");

    const std::string largeAck = std::string("431[{\"ok\":true,\"padding\":\"") +
        std::string(kMaxCloudMatchPayloadBytes, 'x') + "\"}]";
    SocketIoAck ack;
    Require(ParseSocketIoAck(largeAck, ack),
        "legal snapshot response envelope should parse above 64KB");
}

void TestMalformedPacketsAreRejected()
{
    EngineIoOpenPacket open;
    SocketIoNamespaceConnected connected;
    SocketIoConnectError connectError;
    SocketIoEvent event;
    SocketIoAck ack;

    try {
        Require(!ParseEngineIoPacket("0{", open), "malformed Engine.IO JSON should fail");
        Require(!ParseEngineIoPacket(
            R"(0{"sid":"abc","pingInterval":-1,"pingTimeout":20000})", open),
            "negative Engine.IO intervals should fail");
        Require(!ParseSocketIoNamespaceConnected("40[]", connected),
            "namespace connected payload must be an object");
        Require(!ParseSocketIoConnectError("44{", connectError),
            "malformed connect error should fail");
        Require(!ParseSocketIoEvent("42{}", event), "event payload must be an array");
        Require(!ParseSocketIoEvent(R"(42[1,{}])", event),
            "event name must be a string");
        Require(!ParseSocketIoAck("43x[]", ack), "nonnumeric ACK id should fail");
        Require(!ParseSocketIoAck("43[]", ack), "missing ACK id should fail");
        Require(!ParseSocketIoAck("4318446744073709551616[]", ack),
            "overflowing ACK id should fail");
        Require(!ParseSocketIoAck("45unknown", ack), "unknown packet should fail");
        Require(!ParseSocketIoEvent(std::string(kMaxCloudMatchInboundPayloadBytes + 1, 'x'), event),
            "oversized decoded packet should fail");

        WebSocketTextAssembler assembler;
        std::string message;
        const std::string invalidUtf8("\xC3\x28", 2);
        Require(assembler.Add(invalidUtf8, WebSocketBufferKind::utf8Message, message) ==
            WebSocketAssemblyResult::rejected, "invalid UTF-8 should fail");
    }
    catch (...) {
        Require(false, "malformed input must never throw");
    }
}

} // namespace

int main()
{
    TestEngineIoPackets();
    TestSocketIoPackets();
    TestSnapshotUploadEnvelopeLimits();
    TestFragmentAssembly();
    TestMalformedPacketsAreRejected();
    std::cout << "Cloud match protocol tests passed.\n";
    return 0;
}
