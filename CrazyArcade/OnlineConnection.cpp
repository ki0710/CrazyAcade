#include "../NetworkTest/Common/Session.h"
#include "../NetworkTest/Common/Protocol.h"
#include "OnlineConnection.h"
#include <ws2tcpip.h>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

namespace {
using namespace network;
using Clock = std::chrono::steady_clock;
constexpr std::size_t kQueueLimit = 64;

bool Connect(Session& session, const std::string& ip, const std::atomic<bool>& stopped) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9000);
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1 ||
        !SetNonblocking(session.socket)) return false;
    if (connect(session.socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
        return true;
    if (WSAGetLastError() != WSAEWOULDBLOCK) return false;

    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!stopped.load() && Clock::now() < deadline) {
        fd_set writable, failed;
        FD_ZERO(&writable); FD_SET(session.socket, &writable);
        FD_ZERO(&failed); FD_SET(session.socket, &failed);
        timeval timeout{0, 20000};
        const int ready = select(0, nullptr, &writable, &failed, &timeout);
        if (ready == SOCKET_ERROR) return false;
        if (ready > 0) {
            int error = 0, length = sizeof(error);
            return !FD_ISSET(session.socket, &failed) &&
                getsockopt(session.socket, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&error), &length) == 0 && error == 0;
        }
    }
    return false;
}
}

OnlineConnection::OnlineConnection(const std::string& ip)
    : worker_(&OnlineConnection::Run, this, ip) {}

OnlineConnection::~OnlineConnection() {
    Stop();
    worker_.join();
}

bool OnlineConnection::Send(PacketType type, std::uint32_t matchId, std::vector<std::uint8_t> payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_.load() || ended_.load() || outgoing_.size() >= kQueueLimit) return false;
    Packet packet;
    packet.header.packetType = type;
    packet.header.matchId = matchId;
    packet.payload = std::move(payload);
    outgoing_.push_back(std::move(packet));
    return true;
}

std::deque<Packet> OnlineConnection::TakeReceived() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::deque<Packet> packets;
    packets.swap(received_);
    return packets;
}

void OnlineConnection::Run(const std::string& ip) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) == 0) {
        Exchange(ip);
        WSACleanup();
    }
    ended_.store(true);
}

void OnlineConnection::Exchange(const std::string& ip) {
    const auto socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == INVALID_SOCKET) return;
    Session session(socket);
    if (!Connect(session, ip, stop_) || !session.Queue(PacketType::CS_HELLO, HelloPayload())) return;

    bool welcomed = false;
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!stop_.load() && (welcomed || Clock::now() < deadline)) {
        std::deque<Packet> toSend;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            toSend.swap(outgoing_);
        }
        for (const auto& packet : toSend)
            if (!session.Queue(packet.header.packetType, packet.payload, packet.header.matchId)) return;

        fd_set readable, writable;
        FD_ZERO(&readable); FD_SET(socket, &readable);
        FD_ZERO(&writable);
        if (session.WantsWrite()) FD_SET(socket, &writable);
        timeval timeout{0, 20000};
        if (select(0, &readable, &writable, nullptr, &timeout) == SOCKET_ERROR) return;
        if (FD_ISSET(socket, &writable) && !session.Flush()) return;
        if (!FD_ISSET(socket, &readable)) continue;
        const auto result = ReceiveOnce(socket, session.received);
        if (result.status == IoStatus::WouldBlock) continue;
        if (result.status != IoStatus::Progress) return;
        for (;;) {
            Packet packet;
            const auto extracted = session.received.TryExtract(packet);
            if (extracted == ExtractResult::NeedMoreData) break;
            if (extracted != ExtractResult::PacketReady) return;
            if (packet.header.packetType == PacketType::SC_WELCOME) welcomed = true;
            std::lock_guard<std::mutex> lock(mutex_);
            if (received_.size() >= kQueueLimit) return;
            received_.push_back(std::move(packet));
        }
    }
}
