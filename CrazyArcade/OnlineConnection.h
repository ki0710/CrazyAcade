#pragma once
#include "../NetworkTest/Common/Packet.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

// Owns the socket on one worker. The window thread alone interprets game state.
class OnlineConnection {
public:
    explicit OnlineConnection(const std::string& ip);
    ~OnlineConnection();
    bool Send(network::PacketType type, std::uint32_t matchId, std::vector<std::uint8_t> payload);
    std::deque<network::Packet> TakeReceived();
    bool Ended() const { return ended_.load(); }
    void Stop() { stop_.store(true); }

private:
    void Run(const std::string& ip);
    void Exchange(const std::string& ip);

    std::mutex mutex_;
    std::deque<network::Packet> received_, outgoing_;
    std::atomic<bool> stop_{false}, ended_{false};
    std::thread worker_;
};
