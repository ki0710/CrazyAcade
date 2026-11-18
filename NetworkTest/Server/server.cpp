#include "../Common/Session.h"
#include "../Common/Protocol.h"
#include "../Common/ConsoleStop.h"
#include "../Common/MapItemSync.h"
#include "../Common/PlayerMovement.h"
#include "../Common/BombSync.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <limits>
#include <vector>
#include <atomic>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace {
using namespace network;
using Clock = std::chrono::steady_clock;
constexpr unsigned short kPort = 9000;
struct ReceiveEvent {
    std::uint64_t connection;
    IoStatus status;
    Packet packet;
};
class ReceiveQueue {
public:
    bool Push(ReceiveEvent event, const std::atomic<bool>& stop) {
        // Bounded backpressure; never hold the lock while waiting for consumers.
        while (!stop.load()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (events_.size() < 256) {
                    events_.push_back(std::move(event));
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
    std::deque<ReceiveEvent> Take() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::deque<ReceiveEvent> result;
        result.swap(events_);
        return result;
    }
private:
    std::mutex mutex_;
    std::deque<ReceiveEvent> events_;
};
struct Client : Session {
    Client(SOCKET socket, std::uint64_t id, ReceiveQueue& queue)
        : Session(socket), connection(id), events(queue) {}
    ~Client() {
        stop.store(true);
        shutdown(socket, SD_BOTH);
        if (receiver.joinable()) receiver.join();
        // Session closes the socket once, after the receiver has exited.
    }
    void StartReceiver() { receiver = std::thread(&Client::Receive, this); }
    void Receive() {
        ReceiveBuffer buffer;
        bool draining = false;
        while (!stop.load()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket, &readable);
            timeval timeout = {0, 100000};
            const int selected = select(0, &readable, nullptr, nullptr, &timeout);
            if (stop.load()) return;
            if (selected == 0) continue;
            if (selected == SOCKET_ERROR) {
                events.Push({connection, IoStatus::SocketError, {}}, stop);
                return;
            }
            const auto result = ReceiveOnce(socket, buffer);
            if (result.status == IoStatus::WouldBlock) continue;
            if (result.status == IoStatus::Progress) {
                if (draining) { buffer.Reset(); continue; }
                Packet packet;
                for (;;) {
                    const auto extracted = buffer.TryExtract(packet);
                    if (extracted == ExtractResult::NeedMoreData) break;
                    if (extracted == ExtractResult::InvalidData) {
                        if (!events.Push({connection, IoStatus::InvalidData, {}}, stop)) return;
                        draining = true;
                        buffer.Reset();
                        break;
                    }
                    if (!events.Push({connection, IoStatus::Progress, std::move(packet)}, stop)) return;
                }
                continue;
            }
            if (!events.Push({connection, result.status, {}}, stop)) return;
            if (result.status != IoStatus::InvalidData) return;
            // Drain malformed requests until EOF so the loop can send SC_ERROR.
            draining = true;
            buffer.Reset();
        }
    }
    const std::uint64_t connection;
    ReceiveQueue& events;
    std::atomic<bool> stop{false};
    std::thread receiver;
    int playerId = 0;
    bool ready = false;
    bool loaded = false;
    std::uint8_t color = 0;
    bool dead = false;
    bool closing = false;
    bool writeShutdown = false;
    bool readClosed = false;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(5);
};

class Room {
public:
    ReceiveQueue receiveEvents; // Destroyed after clients and their threads.
    std::uint64_t nextConnection = 1;
    std::vector<std::unique_ptr<Client>> clients;
    Client* slots[2] = {nullptr, nullptr};
    MapItemState mapItems;
    enum class Phase { Lobby, Loading, Playing, Finished };
    Phase phase = Phase::Lobby;
    std::uint32_t matchId = 0; // Monotonic for this server lifetime; never reused.
    Clock::time_point loadingDeadline;
    MovingPlayer players[2];
    std::uint32_t moveTick = 0;
    Clock::time_point nextMoveTick;
    Clock::time_point gameEpoch;
    std::vector<ServerBomb> bombs;
    std::deque<int> pendingBombs;
    std::uint32_t nextBombId = 1, bombSequence = 0;
    std::vector<std::uint8_t> lastBombBody;

    explicit Room(int stage = 1) {
        mapItems.stage = stage;
        if (stage == 3) init_board_Ice(mapItems.board);
        else if (stage == 2) init_board_City(mapItems.board);
        else init_board_Forest(mapItems.board);
    }

    // The game loop supplies confirmed bomb hits for one completed update.
    bool ResolveIceHits(const std::vector<int>& playerIds) {
        if (phase != Phase::Playing || mapItems.stage != 3 || mapItems.ice.result != IceResult::Playing) return false;
        bool changed = false;
        for (int playerId : playerIds) {
            if (ApplyIceHit(mapItems.ice, playerId)) changed = true;
        }
        FinishIceTick(mapItems.ice);
        return changed; // Caller sends final world state before SC_MATCH_END.
    }

    // The combat caller supplies newly confirmed tree owners and player hits.
    // Tree awards precede damage, matching the game's explosion/hit order.
    void ResolveForestChanges(const std::vector<int>& treeOwners, const std::vector<int>& playerIds) {
        if (phase != Phase::Playing || mapItems.stage != 1) return;
        bool changed = false;
        for (int playerId : treeOwners) {
            if (playerId < 1 || playerId > 2) continue;
            int& carried = mapItems.forest.carried[playerId - 1];
            if (CollectForestApple(carried)) {
                Broadcast(PacketType::SC_EVENT, EncodeForestAppleEvent(ForestAppleEvent::Gained, playerId, carried), matchId);
                changed = true;
            }
        }
        for (int playerId : playerIds) {
            if (playerId < 1 || playerId > 2) continue;
            int& carried = mapItems.forest.carried[playerId - 1];
            if (LoseForestApples(carried)) {
                Broadcast(PacketType::SC_EVENT, EncodeForestAppleEvent(ForestAppleEvent::Lost, playerId, carried), matchId);
                changed = true;
            }
        }
        if (changed) Broadcast(PacketType::SC_STATE, EncodeForestState(mapItems.forest), matchId);
    }

    void Queue(Client& client, PacketType type, const std::vector<std::uint8_t>& payload,
               std::uint32_t id = 0) {
        if (!client.dead && !client.closing && !client.Queue(type, payload, id)) client.dead = true;
    }
    void Reject(Client& client, ErrorCode error) {
        Queue(client, PacketType::SC_ERROR, ErrorPayload(error));
        client.closing = true;
        client.deadline = Clock::now() + std::chrono::seconds(2);
        std::printf("SC_ERROR: %s\n", ErrorText(error));
    }
    void Broadcast(PacketType type, const std::vector<std::uint8_t>& payload, std::uint32_t id = 0) {
        for (auto* slot : slots) if (slot) Queue(*slot, type, payload, id);
    }
    void Cancel(CancelReason reason) {
        if (phase == Phase::Lobby) return;
        CancelPlantedBombs(bombs, mapItems.board);
        bombs.clear();
        pendingBombs.clear();
        phase = Phase::Lobby;
        for (auto* slot : slots) if (slot) { slot->ready = false; slot->loaded = false; }
        Broadcast(PacketType::SC_MATCH_CANCEL, {static_cast<std::uint8_t>(reason)}, matchId);
        std::printf("SC_MATCH_CANCEL: match=%u reason=%u\n", unsigned(matchId), unsigned(reason));
        BroadcastState();
    }
    bool BothActive() const {
        return slots[0] && slots[1] && !slots[0]->dead && !slots[1]->dead &&
            !slots[0]->closing && !slots[1]->closing;
    }
    bool IsPlaying() const {
        return phase == Phase::Playing &&
            (mapItems.stage != 3 || mapItems.ice.result == IceResult::Playing);
    }
    void BroadcastPlayers() {
        const PlayerFrame frame{moveTick, {players[0].position, players[1].position}};
        Broadcast(PacketType::SC_PLAYER_STATE, EncodePlayerState(frame), matchId);
    }
    void BroadcastBombs(std::uint64_t now) {
        BombFrame frame;
        frame.tick = moveTick;
        if (bombSequence == (std::numeric_limits<std::uint32_t>::max)()) return;
        frame.sequence = bombSequence + 1;
        for (const auto& bomb : bombs) frame.bombs.push_back(ViewBomb(bomb, now));
        const auto payload = EncodeBombState(frame);
        if (payload.empty()) return;
        const std::vector<std::uint8_t> body(payload.begin() + 8, payload.end());
        if (body == lastBombBody) return;
        ++bombSequence;
        lastBombBody = body;
        Broadcast(PacketType::SC_BOMB_STATE, payload, matchId);
    }
    bool NeedsGameTick() const {
        return IsPlaying() || (phase == Phase::Finished && !bombs.empty());
    }
    long WaitMicros(Clock::time_point now) const {
        if (!NeedsGameTick()) return 100000;
        const auto wait = std::chrono::duration_cast<std::chrono::microseconds>(nextMoveTick - now).count();
        return static_cast<long>((std::max)(0LL, (std::min)(20000LL, wait)));
    }
    void TickMovement(Clock::time_point now) {
        if (!NeedsGameTick()) return;
        constexpr auto interval = std::chrono::milliseconds(kMoveTickMs);
        bool changed = false;
        bool advanced = false;
        // Bound catch-up after a stalled process, keeping network servicing responsive.
        for (int steps = 0; steps < 5 && now >= nextMoveTick; ++steps) {
            ++moveTick;
            advanced = true;
            if (IsPlaying())
                for (int i = 0; i < 2; ++i)
                    changed |= players[i].Step(mapItems.board, mapItems.abilities[i].speed);
            nextMoveTick += interval;
        }
        if (now >= nextMoveTick) nextMoveTick = now + interval;
        if (!advanced) return;
        const auto milliseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - gameEpoch).count());
        while (!pendingBombs.empty()) {
            const int owner = pendingBombs.front();
            pendingBombs.pop_front(); // A failed placement also consumes this request.
            if (IsPlaying())
                TryPlaceBomb(bombs, nextBombId, owner, players[owner - 1],
                             mapItems.abilities[owner - 1], mapItems.board, milliseconds);
        }
        const auto changes = UpdateBombs(bombs, mapItems.board, players, milliseconds,
                                        IsPlaying(), mapItems.stage == 1);
        const bool iceChanged = ResolveIceHits(changes.hits);
        const bool ended = phase == Phase::Playing && mapItems.stage == 3 &&
                           mapItems.ice.result != IceResult::Playing;
        if (ended) {
            phase = Phase::Finished;
            pendingBombs.clear();
            for (auto& player : players) player.direction = kInputNone;
            CancelPlantedBombs(bombs, mapItems.board);
        }
        if (changed) BroadcastPlayers();
        for (const auto& cell : changes.broken)
            Broadcast(PacketType::SC_EVENT,
                      EncodeObstacleBroken(cell.x, cell.y, mapItems.board[cell.y][cell.x]), matchId);
        BroadcastBombs(milliseconds);
        if (mapItems.stage == 1) ResolveForestChanges(changes.treeOwners, changes.hits);
        if (iceChanged) Broadcast(PacketType::SC_STATE, EncodeIceState(mapItems.ice), matchId);
        if (ended) Broadcast(PacketType::SC_MATCH_END, EncodeIceState(mapItems.ice), matchId);
    }
    void Tick() {
        if (phase == Phase::Lobby && BothActive() && slots[0]->ready && slots[1]->ready) {
            // Refuse exhaustion instead of reusing an old match identity.
            if (matchId == (std::numeric_limits<std::uint32_t>::max)()) return;
            const int stage = mapItems.stage;
            mapItems = MapItemState{};
            mapItems.stage = stage;
            if (stage == 3) init_board_Ice(mapItems.board);
            else if (stage == 2) init_board_City(mapItems.board);
            else init_board_Forest(mapItems.board);
            for (int i = 0; i < 2; ++i) {
                const auto cell = PlayerStartCell(stage, i + 1);
                if (!CanMoveTo(cell.x, cell.y, mapItems.board)) {
                    slots[0]->ready = slots[1]->ready = false;
                    BroadcastState();
                    return;
                }
                players[i] = MovingPlayer{};
                players[i].position.cell = players[i].position.target = cell;
            }
            const auto snapshot = EncodeMapItemSnapshot(mapItems);
            if (snapshot.empty()) {
                slots[0]->ready = slots[1]->ready = false;
                BroadcastState();
                return;
            }
            ++matchId;
            bombs.clear();
            pendingBombs.clear();
            nextBombId = 1;
            bombSequence = 0;
            lastBombBody.clear();
            phase = Phase::Loading;
            slots[0]->loaded = slots[1]->loaded = false;
            loadingDeadline = Clock::now() + std::chrono::seconds(kLoadingTimeoutSeconds);
            Broadcast(PacketType::SC_SNAPSHOT, snapshot, matchId);
            if (stage == 3) Broadcast(PacketType::SC_STATE, EncodeIceState(mapItems.ice), matchId);
            std::printf("Loading: match=%u\n", unsigned(matchId));
        }
        if (phase != Phase::Loading) return;
        if (!BothActive()) { Cancel(CancelReason::PeerLeft); return; }
        if (Clock::now() >= loadingDeadline) { Cancel(CancelReason::Timeout); return; }
        if (slots[0]->loaded && slots[1]->loaded) {
            phase = Phase::Playing; // Exactly one transition/broadcast, including duplicate LOADED.
            moveTick = 1;
            gameEpoch = Clock::now();
            nextMoveTick = Clock::now() + std::chrono::milliseconds(kMoveTickMs);
            Broadcast(PacketType::SC_MATCH_START, {}, matchId);
            BroadcastPlayers();
            BroadcastBombs(0);
            std::printf("SC_MATCH_START: match=%u\n", unsigned(matchId));
        }
    }
    int HostId() const {
        // clients retains TCP accept order, independent of reused player slots.
        for (const auto& client : clients)
            if (client->playerId && !client->dead && !client->closing) return client->playerId;
        return 0;
    }
    void BroadcastState() {
        const auto count = static_cast<std::uint8_t>((slots[0] != nullptr) + (slots[1] != nullptr));
        const auto first = static_cast<std::uint8_t>(slots[0] && slots[0]->ready);
        const auto second = static_cast<std::uint8_t>(slots[1] && slots[1]->ready);
        const auto map = static_cast<std::uint8_t>(mapItems.stage);
        const auto color1 = slots[0] ? slots[0]->color : std::uint8_t(0);
        const auto color2 = slots[1] ? slots[1]->color : std::uint8_t(0);
        const auto host = static_cast<std::uint8_t>(HostId());
        Broadcast(PacketType::SC_ROOM_STATE, {count, first, second, map, color1, color2, host});
        std::printf("SC_ROOM_STATE: count=%u ready=%u,%u map=%u colors=%u,%u host=%u\n", unsigned(count),
            unsigned(first), unsigned(second), unsigned(map), unsigned(color1), unsigned(color2), unsigned(host));
    }
    void Handle(Client& client, const Packet& packet) {
        if (packet.header.packetType == PacketType::CS_SELECT) {
            if (client.playerId < 1 || client.playerId > 2 ||
                slots[client.playerId - 1] != &client || packet.header.matchId != 0 ||
                !ValidSelectPayload(packet.payload)) {
                Reject(client, ErrorCode::BadRequest);
                return;
            }
            const bool map = packet.payload[0] == static_cast<std::uint8_t>(SelectKind::Map);
            if (phase != Phase::Lobby || (map && client.playerId != HostId())) {
                Queue(client, PacketType::SC_ERROR, ErrorPayload(ErrorCode::SelectionDenied));
                return;
            }
            const auto value = packet.payload[1];
            if (map && mapItems.stage != value) {
                mapItems.stage = value;
                for (auto* slot : slots) if (slot) slot->ready = false;
                BroadcastState();
            } else if (!map && client.color != value) {
                client.color = value;
                client.ready = false;
                BroadcastState();
            }
            return;
        }
        if (packet.header.packetType == PacketType::CS_LOADED) {
            if (client.playerId < 1 || client.playerId > 2 ||
                slots[client.playerId - 1] != &client || !packet.payload.empty() ||
                packet.header.matchId == 0 || packet.header.matchId > matchId) {
                Reject(client, ErrorCode::BadRequest);
                return;
            }
            // Ignore late replies for canceled/started matches and previous attempts.
            if (phase == Phase::Loading && packet.header.matchId == matchId &&
                Clock::now() < loadingDeadline) client.loaded = true;
            return;
        }
        if (packet.header.packetType == PacketType::CS_INPUT) {
            InputData input;
            if (client.playerId < 1 || client.playerId > 2 ||
                slots[client.playerId - 1] != &client ||
                !ReadInputPayload(packet.payload, input) || packet.header.matchId > matchId) {
                Reject(client, ErrorCode::BadRequest);
                return;
            }
            if (packet.header.matchId == 0) {
                // Preserve the console's pre-match transport diagnostic, without moving anyone.
                std::printf("Player %d -> %s, sequence=%u\n", client.playerId,
                    InputDirectionText(input.directionMask), static_cast<unsigned>(input.sequence));
            } else if (IsPlaying() && packet.header.matchId == matchId) {
                if (players[client.playerId - 1].AcceptInput(input) && input.placeBomb) {
                    if (pendingBombs.size() >= 256) Reject(client, ErrorCode::BadRequest);
                    else pendingBombs.push_back(client.playerId);
                }
            }
            return;
        }
        if (packet.header.packetType == PacketType::CS_READY) {
            bool ready;
            if (client.playerId < 1 || client.playerId > 2 ||
                slots[client.playerId - 1] != &client || packet.header.matchId != 0 ||
                !ReadReadyPayload(packet.payload, ready)) {
                Reject(client, ErrorCode::BadRequest);
                return;
            }
            if (client.ready != ready) {
                if (phase == Phase::Loading && !ready) { Cancel(CancelReason::Unready); return; }
                if (phase != Phase::Lobby) return;
                client.ready = ready;
                BroadcastState();
            }
            return;
        }
        if (client.playerId != 0 || packet.header.matchId != 0 ||
            packet.header.packetType != PacketType::CS_HELLO || packet.payload.size() != 4) {
            Reject(client, ErrorCode::BadRequest);
            return;
        }
        if (PayloadWord(packet.payload.data()) != kGameVersion ||
            PayloadWord(packet.payload.data() + 2) != kProtocolVersion) {
            Reject(client, ErrorCode::VersionMismatch);
            return;
        }
        int index = !slots[0] ? 0 : !slots[1] ? 1 : -1;
        if (index < 0) { Reject(client, ErrorCode::RoomFull); return; }
        client.ready = false;
        client.playerId = index + 1;
        slots[index] = &client;
        Queue(client, PacketType::SC_WELCOME, {static_cast<std::uint8_t>(client.playerId)});
        std::printf("SC_WELCOME: Player ID=%d\n", client.playerId);
        BroadcastState();
    }
    void ConsumeReceived() {
        for (auto& event : receiveEvents.Take()) {
            const auto found = std::find_if(clients.begin(), clients.end(),
                [&](const std::unique_ptr<Client>& c) { return c->connection == event.connection; });
            if (found == clients.end() || (*found)->dead) continue;
            auto& client = **found;
            if (event.status == IoStatus::Progress) {
                if (!client.closing) Handle(client, event.packet);
            } else if (event.status == IoStatus::InvalidData) {
                if (!client.closing) Reject(client, ErrorCode::BadRequest);
            } else {
                client.readClosed = true;
                if (!client.closing || event.status != IoStatus::Closed) client.dead = true;
            }
        }
    }
    void RemoveDead() {
        // Repeat because a failed notification may mark another peer dead.
        bool removed;
        do {
            removed = false;
            for (auto& client : clients) {
                if (!client->dead || client->playerId == 0) continue;
                const auto id = static_cast<std::uint8_t>(client->playerId);
                slots[id - 1] = nullptr;
                Cancel(CancelReason::PeerLeft);
                client->playerId = 0;
                removed = true;
                std::printf("Player %u disconnected; slot released\n", unsigned(id));
                Broadcast(PacketType::SC_PEER_LEFT, {id});
                BroadcastState();
            }
        } while (removed);
        clients.erase(std::remove_if(clients.begin(), clients.end(),
            [](const std::unique_ptr<Client>& client) { return client->dead; }), clients.end());
    }
};
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int stage = 1;
    if (argc == 2 && std::strcmp(argv[1], "3") == 0) stage = 3;
    else if (argc > 1) {
        std::printf("Usage: Server.exe [3] (3 selects the ice map)\n");
        return 1;
    }
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) { WSACleanup(); return 1; }
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kPort);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, SOMAXCONN) == SOCKET_ERROR || !SetNonblocking(listener)) {
        std::printf("Server setup failed: %d\n", WSAGetLastError());
        closesocket(listener);
        WSACleanup();
        return 1;
    }
    SetConsoleCtrlHandler(StopHandler, TRUE);
    std::printf("Server listening on port %u. Enter or Ctrl+C to stop.\n", unsigned(kPort));
    int exitCode = 0;
    {
        std::srand(static_cast<unsigned>(Clock::now().time_since_epoch().count()));
        Room room(stage);
        while (!StopRequested()) {
            fd_set readable, writable;
            FD_ZERO(&readable);
            FD_ZERO(&writable);
            // Bound unapproved/rejected sockets as well as the two player slots.
            if (room.clients.size() < 16) FD_SET(listener, &readable);
            for (const auto& client : room.clients) {

                if (client->WantsWrite()) FD_SET(client->socket, &writable);
            }
            timeval timeout = {0, room.WaitMicros(Clock::now())};
            const bool haveSockets = readable.fd_count || writable.fd_count;
            if (!haveSockets) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (haveSockets && select(0, &readable, &writable, nullptr, &timeout) == SOCKET_ERROR) {
                std::printf("select failed: %d\n", WSAGetLastError());
                exitCode = 1;
                break;
            }
            room.ConsumeReceived();
            for (auto& client : room.clients) {
                if (!client->dead && FD_ISSET(client->socket, &writable) && !client->Flush())
                    client->dead = true;
                if (!client->closing && client->playerId == 0 && Clock::now() >= client->deadline)
                    room.Reject(*client, ErrorCode::Timeout);
                if (client->closing && !client->dead) {
                    if (!client->WantsWrite() && !client->writeShutdown) {
                        shutdown(client->socket, SD_SEND);
                        client->writeShutdown = true;
                    }
                    if (client->writeShutdown && client->readClosed) client->dead = true;
                    if (Clock::now() >= client->deadline) client->dead = true;
                }
            }
            room.RemoveDead();
            room.Tick();
            room.TickMovement(Clock::now());
            if (FD_ISSET(listener, &readable)) {
                sockaddr_in peer = {};
                int size = sizeof(peer);
                SOCKET socket = accept(listener, reinterpret_cast<sockaddr*>(&peer), &size);
                if (socket != INVALID_SOCKET) {
                    if (!SetNonblocking(socket)) closesocket(socket);
                    else {
                        std::unique_ptr<Client> client(new Client(socket, room.nextConnection++, room.receiveEvents));
                        if (room.slots[0] && room.slots[1]) room.Reject(*client, ErrorCode::RoomFull);
                        try { client->StartReceiver(); }
                        catch (const std::exception& error) {
                            std::printf("Receive thread failed: %s\n", error.what());
                            continue;
                        }
                        room.clients.push_back(std::move(client));
                    }
                } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
                    std::printf("accept failed: %d\n", WSAGetLastError());
                }
            }
        }
        for (auto& client : room.clients) shutdown(client->socket, SD_BOTH);
        // All Session destructors close sockets before WSACleanup.
    }
    closesocket(listener);
    SetConsoleCtrlHandler(StopHandler, FALSE);
    WSACleanup();
    std::printf("Server stopped.\n");
    return exitCode;
}
