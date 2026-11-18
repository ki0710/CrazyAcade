// Standalone deterministic tests. Do not add this file to a game target.
#include "SocketUtils.h"
#include "Protocol.h"
#include "PlayerMovement.h"
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

static void TestPlayerMovement() {
    using namespace network;
    Board board[MAP_TILES][MAP_TILES]{};
    MovingPlayer p1, p2;
    p1.position.cell = p1.position.target = PlayerStartCell(3, 1);
    p2.position.cell = p2.position.target = PlayerStartCell(3, 2);
    assert(p1.position.cell.x == 6 && p1.position.cell.y == 5);
    assert(p2.position.cell.x == 13 && p2.position.cell.y == 5);
    assert(p1.AcceptInput({kInputRight, 1}));
    assert(p1.Step(board, 1.0f) && p1.position.progress == 250);
    assert(p1.AcceptInput({kInputDown, 2}));
    assert(!p1.AcceptInput({kInputLeft, 2}) && !p1.AcceptInput({kInputUp, 1}));
    for (int i = 0; i < 4; ++i) assert(p1.Step(board, 1.0f));
    assert(p1.position.cell.x == 7 && p1.position.cell.y == 5 && p1.position.progress == 0);
    assert(p1.Step(board, 1.0f) && p1.position.target.y == 6 && p1.position.facing == kInputDown);
    assert(p1.AcceptInput({kInputNone, 3}));
    for (int i = 0; i < 4; ++i) assert(p1.Step(board, 1.0f));
    assert(!p1.Step(board, 1.0f) && p1.position.cell.y == 6);
    assert(p2.AcceptInput({kInputLeft, 1}));
    for (int i = 0; i < 10; ++i) assert(p2.Step(board, 1.0f));
    assert(p2.position.cell.x == 11 && p2.position.cell.y == 5 && p1.position.cell.x == 7);
    assert(p2.AcceptInput({kInputUp, 2}));
    board[4][11].obstacle = 1;
    assert(!p2.Step(board, 1.0f));
    board[4][11].obstacle = 9;
    assert(!p2.Step(board, 1.0f));
    board[4][11] = {0, 0, 0, 3}; // An item alone does not block movement.
    for (int i = 0; i < 5; ++i) assert(p2.Step(board, 1.0f));
    assert(p2.position.cell.y == 4);
    const GridCell edges[] = {{0, 0}, {19, 19}};
    for (GridCell cell : edges) {
        MovingPlayer edge;
        edge.position.cell = edge.position.target = cell;
        assert(edge.AcceptInput({cell.x == 0 ? kInputLeft : kInputRight, 1}));
        assert(!edge.Step(board, 1.0f));
        assert(edge.AcceptInput({cell.y == 0 ? kInputUp : kInputDown, 2}));
        assert(!edge.Step(board, 1.0f));
    }

    assert(p2.Step(board, 1.5f)); // Encode an in-flight position with positive progress.
    const PlayerFrame frame{0x01020304, {p1.position, p2.position}};
    const auto encoded = EncodePlayerState(frame);
    assert(encoded.size() == 18 && encoded[0] == 1 && encoded[3] == 4);
    assert(encoded[4] == 7 && encoded[5] == 6 && encoded[11] == 11 && encoded[12] == 4);
    PlayerFrame decoded;
    assert(ReadPlayerState(encoded, decoded) && EncodePlayerState(decoded) == encoded);
    auto bad = encoded;
    bad.push_back(0);
    assert(!ReadPlayerState(bad, decoded));
    bad = encoded; bad.pop_back();
    assert(!ReadPlayerState(bad, decoded));
    bad = encoded; bad[4] = MAP_TILES;
    assert(!ReadPlayerState(bad, decoded));
    bad = encoded; bad[17] = kInputRight; // Inconsistent target and facing.
    assert(!ReadPlayerState(bad, decoded));
    bad = encoded; bad[16] = 255; bad[15] = 255;
    assert(!ReadPlayerState(bad, decoded));
    assert(EncodePlayerState(decoded) == encoded); // Invalid frames cannot replace state.
    std::vector<std::uint8_t> wire;
    assert(EncodePacket(PacketType::SC_PLAYER_STATE, 3, encoded, wire));
    PacketHeader header;
    assert(DecodeHeader(wire.data(), wire.size(), header) &&
           header.matchId == 3 && header.packetType == PacketType::SC_PLAYER_STATE);
    assert(HelloPayload() == (std::vector<std::uint8_t>{0, 1, 0, 11}));
    std::cout << "Server movement rules and SC_PLAYER_STATE wire tests passed.\n";
}

int main() {
    using namespace network;
    TestPlayerMovement();
    for (PacketType type : {PacketType::CS_LOADED, PacketType::SC_MATCH_START, PacketType::SC_MATCH_CANCEL}) {
        const std::vector<std::uint8_t> payload = type == PacketType::SC_MATCH_CANCEL
            ? std::vector<std::uint8_t>{2} : std::vector<std::uint8_t>{};
        std::vector<std::uint8_t> frame;
        assert(EncodePacket(type, 0x12345678, payload, frame));
        assert(frame.size() == kHeaderSize + payload.size());
        assert(frame[4] == 0 && frame[5] == static_cast<std::uint16_t>(type));
        assert(frame[6] == 0x12 && frame[7] == 0x34 && frame[8] == 0x56 && frame[9] == 0x78);
        for (std::size_t split = 0; split <= frame.size(); ++split) {
            ReceiveBuffer buffer;
            Packet packet;
            assert(buffer.Append(frame.data(), split));
            if (split < frame.size()) {
                assert(buffer.TryExtract(packet) == ExtractResult::NeedMoreData);
                assert(buffer.Append(frame.data() + split, frame.size() - split));
            }
            assert(buffer.TryExtract(packet) == ExtractResult::PacketReady);
            assert(packet.header.matchId == 0x12345678 && packet.header.packetType == type);
            assert(packet.payload == payload);
        }
    }
    std::cout << "Loading/start/cancel framing and match ID tests passed.\n";
    for (int value = 0; value <= 255; ++value) {
        bool ready = true;
        assert(ReadReadyPayload({static_cast<std::uint8_t>(value)}, ready) == (value <= 1));
        if (value <= 1) assert(ready == (value == 1));
        else assert(ready);
    }
    bool ready = true;
    assert(!ReadReadyPayload({}, ready));
    assert(!ReadReadyPayload({1, 0}, ready));
    const std::vector<std::uint8_t> room = {2, 1, 1, 1, 0, 6, 1};
    assert(ValidRoomStatePayload(room));
    assert(ValidRoomStatePayload({0, 0, 0, 3, 0, 0, 0}));
    for (std::size_t size = 0; size <= 10; ++size) {
        auto payload = room;
        payload.resize(size);
        assert(ValidRoomStatePayload(payload) == (size == 7));
    }
    for (std::size_t field = 0; field < room.size(); ++field) {
        auto invalid = room;
        invalid[field] = 255;
        assert(!ValidRoomStatePayload(invalid));
    }
    assert(!ValidRoomStatePayload({2, 0, 0, 0, 0, 0, 1}));
    assert(!ValidRoomStatePayload({2, 0, 0, 1, 0, 0, 0}));
    for (int value = 0; value <= 255; ++value) {
        const auto byte = static_cast<std::uint8_t>(value);
        assert(ValidSelectPayload({1, byte}) == (value >= 1 && value <= 3));
        assert(ValidSelectPayload({2, byte}) == (value <= 6));
        assert(ValidSelectPayload({byte, 1}) == (value == 1 || value == 2));
    }
    assert(!ValidSelectPayload({}));
    assert(!ValidSelectPayload({1}));
    assert(!ValidSelectPayload({2, 0, 1}));
    for (const auto& payload : {std::vector<std::uint8_t>{1, 3}, std::vector<std::uint8_t>{2, 6}}) {
        std::vector<std::uint8_t> frame;
        assert(EncodePacket(PacketType::CS_SELECT, 0, payload, frame));
        assert(frame == std::vector<std::uint8_t>({0, 0, 0, 12, 0, 15, 0, 0, 0, 0, payload[0], payload[1]}));
        for (std::size_t split = 0; split <= frame.size(); ++split) {
            ReceiveBuffer buffer;
            Packet packet;
            assert(buffer.Append(frame.data(), split));
            if (split < frame.size()) {
                assert(buffer.TryExtract(packet) == ExtractResult::NeedMoreData);
                assert(buffer.Append(frame.data() + split, frame.size() - split));
            }
            assert(buffer.TryExtract(packet) == ExtractResult::PacketReady);
            assert(packet.header.packetType == PacketType::CS_SELECT && packet.payload == payload);
        }
    }
    std::cout << "Selection ranges, room state validation and framing tests passed.\n";
    for (std::uint8_t value : {std::uint8_t(0), std::uint8_t(1)}) {
        std::vector<std::uint8_t> frame;
        assert(EncodePacket(PacketType::CS_READY, 0, {value}, frame));
        assert(frame == std::vector<std::uint8_t>({0, 0, 0, 11, 0, 11, 0, 0, 0, 0, value}));
        for (std::size_t split = 0; split <= frame.size(); ++split) {
            ReceiveBuffer buffer;
            Packet packet;
            assert(buffer.Append(frame.data(), split));
            if (split < frame.size()) {
                assert(buffer.TryExtract(packet) == ExtractResult::NeedMoreData);
                assert(buffer.Append(frame.data() + split, frame.size() - split));
            }
            assert(buffer.TryExtract(packet) == ExtractResult::PacketReady);
            assert(ReadReadyPayload(packet.payload, ready) && ready == (value == 1));
        }
    }
    std::cout << "Ready and room state payload tests passed.\n";
    for (std::uint8_t direction : {kInputUp, kInputDown, kInputLeft, kInputRight, kInputNone}) {
        for (std::uint32_t sequence : {0u, 1u, 0x12345678u, 0xFFFFFFFFu}) {
            const auto payload = InputPayload(direction, sequence);
            assert(payload.size() == 6);
            std::vector<std::uint8_t> frame;
            assert(EncodePacket(PacketType::CS_INPUT, 0, payload, frame));
            assert(frame.size() == 16 && frame[3] == 16 && frame[5] == 8);
            assert(frame[10] == direction && frame[11] == (sequence >> 24) &&
                   frame[12] == ((sequence >> 16) & 0xFF) &&
                   frame[13] == ((sequence >> 8) & 0xFF) && frame[14] == (sequence & 0xFF) && frame[15] == 0);
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
        assert(ReadInputPayload({mask, 0, 0, 0, 1, 0}, input) == valid);
        assert(!InputPayload(mask, 1).empty() == valid);
        if (!valid) assert(input.directionMask == kInputRight && input.sequence == 99);
    }
    for (std::size_t size = 0; size <= 10; ++size) {
        if (size == 6) continue;
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
