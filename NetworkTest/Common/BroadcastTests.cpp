// Standalone tests of production Room/Session/PendingSend; never link into apps.
#include "SocketUtils.h"
#include <map>
#include <deque>
#include <future>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::_Exit(1);
    }
}
struct Script {
    // Positive: maximum bytes accepted, zero: closed, negative: Winsock error.
    std::deque<int> results;
    std::vector<std::uint8_t> bytes;
    unsigned calls = 0;
};
std::map<SOCKET, Script> scripts;
int lastError = 0;
int ScriptedSend(SOCKET socket, const char* data, int size, int) {
    auto& script = scripts.at(socket);
    ++script.calls;
    int count = size;
    if (!script.results.empty()) {
        count = script.results.front();
        script.results.pop_front();
    }
    if (count < 0) { lastError = -count; return SOCKET_ERROR; }
    count = (std::min)(count, size);
    script.bytes.insert(script.bytes.end(), data, data + count);
    return count;
}
int ScriptedLastError() { return lastError; }
}

#define send ScriptedSend
#define WSAGetLastError ScriptedLastError
#include "SocketUtils.cpp"
#undef send
#undef WSAGetLastError
#define main BroadcastTestServerMain
#include "../Server/server.cpp"
#undef main

namespace {
Client& AddClient(Room& room, int id) {
    const auto socket = ::socket(AF_INET, SOCK_STREAM, 0);
    Check(socket != INVALID_SOCKET, "create owned test socket");
    scripts[socket] = Script{};
    room.clients.emplace_back(new Client(socket, room.nextConnection++, room.receiveEvents));
    auto& client = *room.clients.back();
    client.playerId = id;
    room.slots[id - 1] = &client;
    return client; // No receiver thread: only send results are scripted.
}
std::vector<std::uint8_t> Wire(PacketType type, const std::vector<std::uint8_t>& payload,
                               std::uint32_t id) {
    std::vector<std::uint8_t> bytes;
    Check(EncodePacket(type, id, payload, bytes), "encode expected wire");
    return bytes;
}
void Append(std::vector<std::uint8_t>& target, const std::vector<std::uint8_t>& bytes) {
    target.insert(target.end(), bytes.begin(), bytes.end());
}
void PartialBroadcast() {
    Room room;
    auto& first = AddClient(room, 1);
    auto& second = AddClient(room, 2);
    const auto firstSocket = first.socket, secondSocket = second.socket;
    // Both peers block repeatedly at independent offsets, including a frame boundary.
    scripts[firstSocket].results = {1, -WSAEWOULDBLOCK, 2, -WSAEWOULDBLOCK,
                                    9, -WSAEWOULDBLOCK, 1, -WSAEWOULDBLOCK};
    scripts[secondSocket].results = {-WSAEWOULDBLOCK, 4, -WSAEWOULDBLOCK,
                                     8, -WSAEWOULDBLOCK};
    std::vector<std::uint8_t> expected;
    for (const auto& payload : {std::vector<std::uint8_t>{1, 2},
                                std::vector<std::uint8_t>{3, 4, 5}}) {
        room.Broadcast(PacketType::SC_STATE, payload, 17);
        Append(expected, Wire(PacketType::SC_STATE, payload, 17));
    }
    Check(scripts[firstSocket].calls == 0 && scripts[secondSocket].calls == 0,
          "Broadcast must enqueue without synchronous socket I/O");
    Check(first.Flush() && first.WantsWrite(), "first partial frame must remain queued");
    Check(second.Flush() && second.WantsWrite(), "second blocked frame must remain queued");
    for (unsigned n = 0; n < 10 && (first.WantsWrite() || second.WantsWrite()); ++n)
        Check(first.Flush() && second.Flush(), "resume independent queues");
    Check(!first.WantsWrite() && !second.WantsWrite(), "both queues must drain");
    Check(scripts[firstSocket].bytes == expected && scripts[secondSocket].bytes == expected,
          "partial sends must preserve exact FIFO bytes without duplication or interleaving");
    Check(first.Flush() && second.Flush(), "empty flush");
    Check(scripts[firstSocket].bytes == expected && scripts[secondSocket].bytes == expected,
          "empty flush must not resend");
    std::puts("PASS: two Broadcast recipients, repeated short sends/would-block, FIFO exact bytes");
}
void FullSendQueue(int saturatedId) {
    Room room;
    auto& first = AddClient(room, 1);
    auto& second = AddClient(room, 2);
    auto& full = saturatedId == 1 ? first : second;
    auto& healthy = saturatedId == 1 ? second : first;
    const auto fullSocket = full.socket;
    for (int n = 0; n < 64; ++n)
        Check(full.Queue(PacketType::SC_STATE, {static_cast<std::uint8_t>(n)}, 17), "fill send queue");
    scripts[fullSocket].results = {-WSAEWOULDBLOCK};
    Check(full.Flush() && full.WantsWrite(), "blocked queue must remain full");
    room.Broadcast(PacketType::SC_STATE, {99}, 17);
    Check(full.dead && !healthy.dead, "overflow must kill only saturated recipient");
    Check(healthy.Flush() && scripts[healthy.socket].bytes == Wire(PacketType::SC_STATE, {99}, 17),
          "overflow must not skip healthy recipient in either slot order");
    room.RemoveDead();
    Check(room.slots[saturatedId - 1] == nullptr && room.clients.size() == 1,
          "remove overflowed connection and free slot");
    Check(healthy.Flush() && !healthy.dead, "departure notifications must drain at healthy peer");
    room.Broadcast(PacketType::SC_STATE, {100}, 18);
    Check(healthy.Flush(), "survivor must continue receiving broadcasts");
    const auto& bytes = scripts[healthy.socket].bytes;
    const auto tail = Wire(PacketType::SC_STATE, {100}, 18);
    Check(bytes.size() >= tail.size() && std::equal(tail.rbegin(), tail.rend(), bytes.rbegin()),
          "survivor final frame intact");
    std::puts("PASS: full 64-packet queue isolated, slot released, surviving peer continues");
}
void FailedSend(int result) {
    Room room;
    auto& failed = AddClient(room, 1);
    auto& healthy = AddClient(room, 2);
    const auto failedSocket = failed.socket;
    scripts[failedSocket].results = {3, result};
    room.Broadcast(PacketType::SC_STATE, {1, 2, 3}, 17);
    // Same failure-to-dead transition used by the production writable event loop.
    if (!failed.Flush()) failed.dead = true;
    Check(failed.dead && scripts[failedSocket].bytes.size() == 3, "failure after partial frame");
    room.Broadcast(PacketType::SC_STATE, {4}, 17);
    Check(healthy.Flush(), "failed send must not poison other session");
    auto expected = Wire(PacketType::SC_STATE, {1, 2, 3}, 17);
    Append(expected, Wire(PacketType::SC_STATE, {4}, 17));
    Check(scripts[healthy.socket].bytes == expected, "survivor exact frames after send failure");
    const auto calls = scripts[failedSocket].calls;
    room.RemoveDead();
    Check(room.slots[0] == nullptr && healthy.Flush(), "failed peer removal and notification");
    Check(scripts[failedSocket].calls == calls, "no send to removed peer");
    std::puts("PASS: partial-frame send failure/closure isolated and removed safely");
}
void ReceiveClosureAndClosing() {
    Room room;
    auto& departing = AddClient(room, 1);
    auto& healthy = AddClient(room, 2);
    const auto socket = departing.socket;
    room.phase = Room::Phase::Playing;
    room.matchId = 17;
    room.Broadcast(PacketType::SC_STATE, {1}, 17);
    std::atomic<bool> stop{false};
    Check(room.receiveEvents.Push({departing.connection, IoStatus::Closed, {}}, stop), "enqueue EOF");
    room.ConsumeReceived();
    Check(departing.dead, "EOF marks departing connection dead");
    room.Broadcast(PacketType::SC_STATE, {2}, 17);
    room.RemoveDead();
    Check(room.phase == Room::Phase::Lobby && room.slots[0] == nullptr, "disconnect cancels match");
    Check(healthy.Flush(), "survivor drains state and cancellation");
    auto expected = Wire(PacketType::SC_STATE, {1}, 17);
    Append(expected, Wire(PacketType::SC_STATE, {2}, 17));
    Append(expected, Wire(PacketType::SC_MATCH_CANCEL, {1}, 17));
    const std::vector<std::uint8_t> lobby{1, 0, 0, 1, 0, 0, 2};
    Append(expected, Wire(PacketType::SC_ROOM_STATE, lobby, 0));
    Append(expected, Wire(PacketType::SC_PEER_LEFT, {1}, 0));
    Append(expected, Wire(PacketType::SC_ROOM_STATE, lobby, 0));
    Check(scripts[healthy.socket].bytes == expected && scripts[socket].calls == 0,
          "queued disconnect keeps survivor ordering and never flushes dead peer");
    auto& closing = AddClient(room, 1);
    closing.closing = true;
    room.Broadcast(PacketType::SC_STATE, {3}, 18);
    Check(!closing.WantsWrite() && healthy.Flush(), "closing peer excluded without blocking survivor");
    std::puts("PASS: queued EOF during Broadcast, match cancellation ordering and closing exclusion");
}
void ReceiveBackpressure() {
    Room room;
    auto& first = AddClient(room, 1);
    auto& second = AddClient(room, 2);
    std::atomic<bool> stop{false};
    for (unsigned n = 0; n < 256; ++n)
        Check(room.receiveEvents.Push({99, IoStatus::Progress, {}}, stop), "fill input queue");
    std::promise<void> entered;
    auto producer = std::async(std::launch::async, [&] {
        entered.set_value();
        return room.receiveEvents.Push({99, IoStatus::Closed, {}}, stop);
    });
    entered.get_future().wait();
    Check(producer.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout,
          "producer must wait on full input queue");
    room.Broadcast(PacketType::SC_STATE, {7}, 17);
    Check(first.Flush() && second.Flush(), "full receive queue must not block Broadcast or Flush");
    const auto expected = Wire(PacketType::SC_STATE, {7}, 17);
    Check(scripts[first.socket].bytes == expected && scripts[second.socket].bytes == expected,
          "Broadcast unaffected by input backpressure");
    Check(room.receiveEvents.Take().size() == 256, "Broadcast must preserve queued input");
    Check(producer.wait_for(std::chrono::seconds(5)) == std::future_status::ready && producer.get(),
          "input producer resumes after drain");
    Check(room.receiveEvents.Take().size() == 1, "resumed input preserved exactly once");
    std::puts("PASS: full CRITICAL_SECTION receive queue and blocked producer do not block Broadcast");
}
}

int main() {
    std::promise<void> finished;
    auto done = finished.get_future();
    std::thread watchdog([&] {
        Check(done.wait_for(std::chrono::seconds(30)) == std::future_status::ready,
              "Broadcast tests timed out (possible deadlock)");
    });
    WSADATA wsa;
    Check(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "WSAStartup");
    PartialBroadcast();
    FullSendQueue(1);
    FullSendQueue(2);
    FailedSend(-WSAECONNRESET);
    FailedSend(0);
    ReceiveClosureAndClosing();
    ReceiveBackpressure();
    WSACleanup();
    finished.set_value();
    watchdog.join();
    std::puts("All Broadcast stability tests passed.");
}
