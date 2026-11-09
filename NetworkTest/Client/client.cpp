#include "../Common/Session.h"
#include "../Common/Protocol.h"
#include "../Common/ConsoleStop.h"
#include <ws2tcpip.h>
#include <chrono>
#include <cstdio>

#pragma comment(lib, "ws2_32.lib")

namespace {
using namespace network;
bool Handle(const Packet& packet, int& playerId, bool& rejected) {
    if (packet.header.matchId != 0) return false;
    const auto& payload = packet.payload;
    switch (packet.header.packetType) {
    case PacketType::SC_WELCOME:
        if (playerId || payload.size() != 1 || payload[0] < 1 || payload[0] > 2) return false;
        playerId = payload[0];
        std::printf("SC_WELCOME: Player ID=%d\n", playerId);
        return true;
    case PacketType::SC_ROOM_STATE:
        if (!playerId || payload.size() != 1 || payload[0] < 1 || payload[0] > 2) return false;
        std::printf("SC_ROOM_STATE: count=%u\n", unsigned(payload[0]));
        return true;
    case PacketType::SC_PEER_LEFT:
        if (!playerId || payload.size() != 1 || payload[0] < 1 || payload[0] > 2 ||
            payload[0] == playerId) return false;
        std::printf("SC_PEER_LEFT: Player ID=%u\n", unsigned(payload[0]));
        return true;
    case PacketType::SC_ERROR: {
        if (payload.size() != 2) return false;
        const auto code = static_cast<ErrorCode>(PayloadWord(payload.data()));
        const char* text = ErrorText(code);
        if (!text) return false;
        std::printf("SC_ERROR: %s (code=%u)\n", text, unsigned(code));
        rejected = true;
        return true;
    }
    default: return false;
    }
}
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == INVALID_SOCKET) { WSACleanup(); return 1; }
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    address.sin_port = htons(9000);
    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        !SetNonblocking(socket)) {
        std::printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(socket);
        WSACleanup();
        return 1;
    }
    SetConsoleCtrlHandler(StopHandler, TRUE);
    std::printf("Connected to 127.0.0.1:9000. Enter or Ctrl+C to exit.\n");
    int exitCode = 0;
    {
        Session session(socket);
        int playerId = 0;
        bool rejected = false;
        bool running = session.Queue(PacketType::CS_HELLO, HelloPayload());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (running && !StopRequested()) {
            fd_set readable, writable;
            FD_ZERO(&readable);
            FD_ZERO(&writable);
            FD_SET(socket, &readable);
            if (session.WantsWrite()) FD_SET(socket, &writable);
            timeval timeout = {0, 100000};
            if (select(0, &readable, &writable, nullptr, &timeout) == SOCKET_ERROR) {
                std::printf("select failed: %d\n", WSAGetLastError());
                exitCode = 1;
                break;
            }
            if (FD_ISSET(socket, &writable) && !session.Flush()) {
                std::printf("Send failed.\n"); exitCode = 1; break;
            }
            if (FD_ISSET(socket, &readable)) {
                const auto result = ReceiveOnce(socket, session.received);
                if (result.status == IoStatus::Closed) {
                    std::printf("Server disconnected.\n"); break;
                }
                if (result.status != IoStatus::Progress && result.status != IoStatus::WouldBlock) {
                    std::printf("Receive failed: status=%d error=%d\n", int(result.status), result.error);
                    exitCode = 1; break;
                }
                Packet packet;
                while (running) {
                    const auto extracted = session.received.TryExtract(packet);
                    if (extracted == ExtractResult::NeedMoreData) break;
                    if (extracted == ExtractResult::InvalidData || !Handle(packet, playerId, rejected)) {
                        std::printf("Invalid server packet.\n"); exitCode = 1; running = false;
                    }
                    if (rejected) { exitCode = 2; running = false; }
                }
            }
            if (!playerId && std::chrono::steady_clock::now() >= deadline) {
                std::printf("WELCOME timeout.\n"); exitCode = 1; break;
            }
        }
        shutdown(socket, SD_BOTH);
    }
    SetConsoleCtrlHandler(StopHandler, FALSE);
    WSACleanup();
    std::printf("Client stopped.\n");
    return exitCode;
}
