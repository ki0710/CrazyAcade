#pragma once
#include "SocketUtils.h"
#include <deque>

namespace network {
// Network-thread-owned queue. Never interleave partial packets on one socket.
class Session {
public:
    explicit Session(SOCKET value) : socket(value) {}
    ~Session() { closesocket(socket); }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    bool Queue(PacketType type, const std::vector<std::uint8_t>& payload) {
        if (outgoing_.size() >= 64) return false;
        PendingSend packet;
        if (!packet.Prepare(type, 0, payload)) return false;
        outgoing_.push_back(std::move(packet));
        return true;
    }
    bool WantsWrite() const { return !outgoing_.empty(); }
    bool Flush() {
        while (!outgoing_.empty()) {
            const auto result = outgoing_.front().Flush(socket);
            if (result.status == IoStatus::WouldBlock) return true;
            if (result.status != IoStatus::Complete) return false;
            outgoing_.pop_front();
        }
        return true;
    }
    SOCKET socket;
    ReceiveBuffer received;
private:
    std::deque<PendingSend> outgoing_;
};
inline bool SetNonblocking(SOCKET socket) {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}
} // namespace network
