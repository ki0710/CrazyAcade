#include "../Common/Session.h"
#include "../Common/Protocol.h"
#include "../Common/ConsoleStop.h"
#include "../Common/MapItemSync.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {
using namespace network;
using Clock = std::chrono::steady_clock;
constexpr unsigned short kPort = 9000;
struct Client : Session {
    explicit Client(SOCKET socket) : Session(socket) {}
    int playerId = 0;
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

    Room() {
        mapItems.stage = 1;
        init_board_Forest(mapItems.board);
    }

    void Queue(Client& client, PacketType type, const std::vector<std::uint8_t>& payload) {
        if (!client.dead && !client.closing && !client.Queue(type, payload)) client.dead = true;
    }
    void Reject(Client& client, ErrorCode error) {
        Queue(client, PacketType::SC_ERROR, ErrorPayload(error));
        client.closing = true;
        client.deadline = Clock::now() + std::chrono::seconds(2);
        std::printf("SC_ERROR: %s\n", ErrorText(error));
    }
    void Broadcast(PacketType type, const std::vector<std::uint8_t>& payload) {
        for (auto* slot : slots) if (slot) Queue(*slot, type, payload);
    }
    void BroadcastState() {
        const auto count = static_cast<std::uint8_t>((slots[0] != nullptr) + (slots[1] != nullptr));
        Broadcast(PacketType::SC_ROOM_STATE, {count});
        std::printf("SC_ROOM_STATE: count=%u\n", unsigned(count));
    }
    void Handle(Client& client, const Packet& packet) {
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
        client.playerId = index + 1;
        slots[index] = &client;
        Queue(client, PacketType::SC_WELCOME, {static_cast<std::uint8_t>(client.playerId)});
        std::printf("SC_WELCOME: Player ID=%d\n", client.playerId);
        BroadcastState();
        if (slots[0] && slots[1]) {
            const auto snapshot = EncodeMapItemSnapshot(mapItems);
            Broadcast(PacketType::SC_SNAPSHOT, snapshot);
        }
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

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
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
    Room room;
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
