#include "../Common/Session.h"
#include "../Common/Protocol.h"
#include "../Common/ConsoleStop.h"
#include "../Common/MapItemSync.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <limits>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {
using namespace network;
using Clock = std::chrono::steady_clock;
constexpr unsigned short kPort = 9000;
struct Client : Session {
    explicit Client(SOCKET socket) : Session(socket) {}
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
    std::vector<std::unique_ptr<Client>> clients;
    Client* slots[2] = {nullptr, nullptr};
    MapItemState mapItems;
    enum class Phase { Lobby, Loading, Playing };
    Phase phase = Phase::Lobby;
    std::uint32_t matchId = 0; // Monotonic for this server lifetime; never reused.
    Clock::time_point loadingDeadline;

    explicit Room(int stage = 1) {
        mapItems.stage = stage;
        if (stage == 3) init_board_Ice(mapItems.board);
        else if (stage == 2) init_board_City(mapItems.board);
        else init_board_Forest(mapItems.board);
    }

    // The game loop supplies confirmed bomb hits for one completed update.
    void ResolveIceHits(const std::vector<int>& playerIds) {
        if (phase != Phase::Playing || mapItems.stage != 3 || mapItems.ice.result != IceResult::Playing) return;
        bool changed = false;
        for (int playerId : playerIds) {
            if (ApplyIceHit(mapItems.ice, playerId)) changed = true;
        }
        const bool ended = FinishIceTick(mapItems.ice);
        if (changed) Broadcast(PacketType::SC_STATE, EncodeIceState(mapItems.ice), matchId);
        if (ended) Broadcast(PacketType::SC_MATCH_END, EncodeIceState(mapItems.ice), matchId);
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
            const auto snapshot = EncodeMapItemSnapshot(mapItems);
            if (snapshot.empty()) {
                slots[0]->ready = slots[1]->ready = false;
                BroadcastState();
                return;
            }
            ++matchId;
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
            Broadcast(PacketType::SC_MATCH_START, {}, matchId);
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
            if (client.playerId < 1 || client.playerId > 2 || packet.header.matchId != 0 ||
                !ReadInputPayload(packet.payload, input)) {
                Reject(client, ErrorCode::BadRequest);
                return;
            }
            // Input transport test only: no Player/Board movement or broadcast here.
            std::printf("Player %d -> %s, sequence=%u\n", client.playerId,
                InputDirectionText(input.directionMask), static_cast<unsigned>(input.sequence));
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
    void Read(Client& client) {
        const auto result = ReceiveOnce(client.socket, client.received);
        if (result.status == IoStatus::WouldBlock) return;
        if (result.status == IoStatus::InvalidData) { Reject(client, ErrorCode::BadRequest); return; }
        if (result.status != IoStatus::Progress) { client.dead = true; return; }
        Packet packet;
        while (!client.closing && !client.dead) {
            const auto extracted = client.received.TryExtract(packet);
            if (extracted == ExtractResult::NeedMoreData) break;
            if (extracted == ExtractResult::InvalidData) { Reject(client, ErrorCode::BadRequest); break; }
            Handle(client, packet);
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
                if (!client->readClosed) FD_SET(client->socket, &readable);
                if (client->WantsWrite()) FD_SET(client->socket, &writable);
            }
            timeval timeout = {0, 100000};
            if (select(0, &readable, &writable, nullptr, &timeout) == SOCKET_ERROR) {
                std::printf("select failed: %d\n", WSAGetLastError());
                exitCode = 1;
                break;
            }
            // Read existing peers and free departed slots before accepting/approving new ones.
            for (auto& client : room.clients) {
                if (FD_ISSET(client->socket, &readable)) {
                    if (!client->closing) room.Read(*client);
                    else {
                        // Drain requests already in flight during rejection. Closing a
                        // socket with unread data can reset TCP and lose SC_ERROR.
                        client->received.Reset();
                        const auto result = ReceiveOnce(client->socket, client->received);
                        client->received.Reset();
                        if (result.status == IoStatus::Closed) client->readClosed = true;
                        else if (result.status != IoStatus::Progress && result.status != IoStatus::WouldBlock)
                            client->dead = true;
                    }
                }
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
            if (FD_ISSET(listener, &readable)) {
                sockaddr_in peer = {};
                int size = sizeof(peer);
                SOCKET socket = accept(listener, reinterpret_cast<sockaddr*>(&peer), &size);
                if (socket != INVALID_SOCKET) {
                    if (!SetNonblocking(socket)) closesocket(socket);
                    else {
                        std::unique_ptr<Client> client(new Client(socket));
                        if (room.slots[0] && room.slots[1]) room.Reject(*client, ErrorCode::RoomFull);
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
