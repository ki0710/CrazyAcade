// Standalone deterministic tests. Do not add this file to a game target.
#include "SocketUtils.h"
#include "Protocol.h"
#include <algorithm>
#include <cassert>
#include <iostream>

namespace {
std::vector<std::uint8_t> sentBytes;
std::vector<std::uint8_t> incoming;
int sendCalls = 0;
int lastError = 0;
int receiveMode = 0;
int FakeSend(SOCKET, const char* data, int size, int) {
    ++sendCalls;
    if (sendCalls == 2) { lastError = WSAEWOULDBLOCK; return SOCKET_ERROR; }
    const int count = (std::min)(size, 3);
    sentBytes.insert(sentBytes.end(), data, data + count);
    return count;
}
int FakeRecv(SOCKET, char* data, int size, int) {
    assert(size == 4096);
    if (receiveMode == 1) { lastError = WSAEWOULDBLOCK; return SOCKET_ERROR; }
    if (receiveMode == 2) return 0;
    if (receiveMode == 3) { lastError = WSAECONNRESET; return SOCKET_ERROR; }
    const auto count = (std::min)(incoming.size(), static_cast<std::size_t>(size));
    std::copy(incoming.begin(), incoming.begin() + count, data);
    incoming.erase(incoming.begin(), incoming.begin() + count);
    return static_cast<int>(count);
}
int FakeLastError() { return lastError; }
}

// Substitute only system I/O; exercise the actual production implementation.
#define send FakeSend
#define recv FakeRecv
#define WSAGetLastError FakeLastError
#include "SocketUtils.cpp"
#undef send
#undef recv
#undef WSAGetLastError

int main() {
    using namespace network;
    for (std::uint8_t direction : {kInputUp, kInputDown, kInputLeft, kInputRight, kInputNone}) {
        for (std::uint32_t sequence : {0u, 1u, 0x12345678u, 0xFFFFFFFFu}) {
            const auto payload = InputPayload(direction, sequence);
            assert(payload.size() == 5);
            std::vector<std::uint8_t> frame;
            assert(EncodePacket(PacketType::CS_INPUT, 0, payload, frame));
            assert(frame.size() == 15 && frame[3] == 15 && frame[5] == 8);
            assert(frame[10] == direction && frame[11] == (sequence >> 24) &&
                   frame[12] == ((sequence >> 16) & 0xFF) &&
                   frame[13] == ((sequence >> 8) & 0xFF) && frame[14] == (sequence & 0xFF));
            for (std::size_t split = 0; split <= frame.size(); ++split) {
                ReceiveBuffer buffer;
                assert(buffer.Append(frame.data(), split));
                Packet inputPacket;
                if (split < frame.size()) {
                    assert(buffer.TryExtract(inputPacket) == ExtractResult::NeedMoreData);
                    assert(buffer.Append(frame.data() + split, frame.size() - split));
                }
                assert(buffer.TryExtract(inputPacket) == ExtractResult::PacketReady);
                assert(inputPacket.header.packetType == PacketType::CS_INPUT);
                InputData input;
                assert(ReadInputPayload(inputPacket.payload, input));
                assert(input.directionMask == direction && input.sequence == sequence);
            }
        }
    }
    for (int direction = 0; direction <= 255; ++direction) {
        const bool valid = direction == 0 || direction == 1 || direction == 2 ||
                           direction == 4 || direction == 8;
        const auto mask = static_cast<std::uint8_t>(direction);
        InputData input{kInputRight, 99};
        assert(ReadInputPayload({mask, 0, 0, 0, 1}, input) == valid);
        assert(!InputPayload(mask, 1).empty() == valid);
        if (!valid) assert(input.directionMask == kInputRight && input.sequence == 99);
    }
    for (std::size_t size = 0; size <= 10; ++size) {
        if (size == 5) continue;
        InputData input{kInputRight, 99};
        assert(!ReadInputPayload(std::vector<std::uint8_t>(size, 0), input));
        assert(input.directionMask == kInputRight && input.sequence == 99);
    }
    std::cout << "CS_INPUT tests passed: directions, sequences, wire bytes, splits and invalid payloads.\n";

    std::vector<std::uint8_t> encoded;
    assert(EncodePacket(PacketType::SC_ROOM_STATE, 0x12345678, {0xAB, 0xCD}, encoded));
    const std::vector<std::uint8_t> expected = {
        0, 0, 0, 12, 0, 3, 0x12, 0x34, 0x56, 0x78, 0xAB, 0xCD};
    assert(encoded == expected);
    Packet packet;
    for (std::size_t split = 0; split <= encoded.size(); ++split) {
        ReceiveBuffer buffer;
        assert(buffer.Append(encoded.data(), split));
        if (split < encoded.size()) {
            assert(buffer.TryExtract(packet) == ExtractResult::NeedMoreData);
            assert(buffer.Append(encoded.data() + split, encoded.size() - split));
        }
        assert(buffer.TryExtract(packet) == ExtractResult::PacketReady);
        assert(packet.header.matchId == 0x12345678 && packet.payload.size() == 2);
        assert(buffer.TryExtract(packet) == ExtractResult::NeedMoreData);
    }
    ReceiveBuffer merged;
    assert(merged.Append(encoded.data(), encoded.size()));
    assert(merged.Append(encoded.data(), encoded.size()));
    assert(merged.TryExtract(packet) == ExtractResult::PacketReady);
    assert(merged.TryExtract(packet) == ExtractResult::PacketReady);
    assert(!merged.HasIncompletePacket());

    for (int kind = 0; kind < 3; ++kind) {
        auto bad = encoded;
        if (kind == 0) bad[3] = 9; // Smaller than header.
        if (kind == 1) bad[0] = 1; // Exceeds maximum.
        if (kind == 2) bad[5] = 99; // Unknown type.
        ReceiveBuffer buffer;
        assert(buffer.Append(bad.data(), bad.size()));
        assert(buffer.TryExtract(packet) == ExtractResult::InvalidData);
        assert(!buffer.Append(encoded.data(), encoded.size()));
        buffer.Reset();
        assert(buffer.Append(encoded.data(), encoded.size()));
    }
    const auto preserved = encoded;
    assert(!EncodePacket(static_cast<PacketType>(0), 0, {}, encoded));
    assert(encoded == preserved);
    assert(!EncodePacket(PacketType::CS_HELLO, 0,
        std::vector<std::uint8_t>(kMaxPacketSize), encoded));

    std::vector<std::uint8_t> large;
    assert(EncodePacket(PacketType::CS_HELLO, 0,
        std::vector<std::uint8_t>(kMaxPacketSize - kHeaderSize, 42), large));
    ReceiveBuffer largeBuffer;
    for (std::size_t offset = 0; offset < large.size();) {
        const auto count = (std::min)(kReceiveBufferSize, large.size() - offset);
        assert(largeBuffer.Append(large.data() + offset, count));
        offset += count;
        assert(largeBuffer.TryExtract(packet) == (offset == large.size()
            ? ExtractResult::PacketReady : ExtractResult::NeedMoreData));
    }
    assert(packet.payload.size() == kMaxPacketSize - kHeaderSize);

    PendingSend pending;
    assert(pending.Prepare(PacketType::SC_ROOM_STATE, 0x12345678, {0xAB, 0xCD}));
    assert(pending.Flush(INVALID_SOCKET).status == IoStatus::WouldBlock);
    assert(sentBytes.size() == 3);
    assert(!pending.Prepare(PacketType::CS_HELLO, 0, {}));
    assert(pending.Flush(INVALID_SOCKET).status == IoStatus::Complete);
    assert(sentBytes == expected);
    assert(pending.Flush(INVALID_SOCKET).status == IoStatus::Complete);
    assert(sentBytes == expected);

    ReceiveBuffer received;
    incoming = expected;
    assert(ReceiveOnce(INVALID_SOCKET, received).status == IoStatus::Progress);
    assert(received.TryExtract(packet) == ExtractResult::PacketReady);
    receiveMode = 1;
    assert(ReceiveOnce(INVALID_SOCKET, received).status == IoStatus::WouldBlock);
    receiveMode = 3;
    assert(ReceiveOnce(INVALID_SOCKET, received).error == WSAECONNRESET);
    receiveMode = 2;
    assert(ReceiveOnce(INVALID_SOCKET, received).status == IoStatus::Closed);
    assert(received.Append(expected.data(), 3));
    assert(ReceiveOnce(INVALID_SOCKET, received).status == IoStatus::InvalidData);
    std::cout << "All packet and socket utility tests passed.\n";
}
