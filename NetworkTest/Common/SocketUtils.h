#pragma once

#include <winsock2.h>
#include "Packet.h"

namespace network {
enum class IoStatus { Progress, Complete, WouldBlock, Closed, SocketError, InvalidData };
struct IoResult {
    IoStatus status;
    int error = 0;
    IoResult(IoStatus value, int code = 0) : status(value), error(code) {}
};

// Performs one recv with a 4096-byte buffer. Extract all available packets after
// Progress; WouldBlock returns immediately. Closed with buffered bytes indicates
// a truncated frame (drain complete packets before calling again).
IoResult ReceiveOnce(SOCKET socket, ReceiveBuffer& buffer);

// Owns encoded bytes and the partial-send cursor. One pending packet per socket;
// serialize access so another sender cannot interleave a frame's bytes.
class PendingSend {
public:
    bool Prepare(PacketType type, std::uint32_t matchId,
                 const std::vector<std::uint8_t>& payload);
    bool Complete() const { return offset_ == bytes_.size(); }
    IoResult Flush(SOCKET socket);
private:
    std::vector<std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};
} // namespace network
