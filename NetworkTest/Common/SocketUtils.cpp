#include "SocketUtils.h"

#pragma comment(lib, "ws2_32.lib")

namespace network {
IoResult ReceiveOnce(SOCKET socket, ReceiveBuffer& buffer) {
    std::uint8_t chunk[kReceiveBufferSize];
    const int received = recv(socket, reinterpret_cast<char*>(chunk),
                              static_cast<int>(sizeof(chunk)), 0);
    if (received > 0)
        return buffer.Append(chunk, static_cast<std::size_t>(received))
            ? IoStatus::Progress : IoStatus::InvalidData;
    if (received == 0)
        return buffer.HasIncompletePacket() ? IoStatus::InvalidData : IoStatus::Closed;
    const int error = WSAGetLastError();
    return {error == WSAEWOULDBLOCK ? IoStatus::WouldBlock : IoStatus::SocketError, error};
}

bool PendingSend::Prepare(PacketType type, std::uint32_t matchId,
                          const std::vector<std::uint8_t>& payload) {
    if (!Complete()) return false;
    if (!EncodePacket(type, matchId, payload, bytes_)) return false;
    offset_ = 0;
    return true;
}

IoResult PendingSend::Flush(SOCKET socket) {
    while (!Complete()) {
        const int sent = send(socket,
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            static_cast<int>(bytes_.size() - offset_), 0);
        if (sent > 0) {
            offset_ += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent == 0) return IoStatus::Closed;
        const int error = WSAGetLastError();
        return {error == WSAEWOULDBLOCK ? IoStatus::WouldBlock : IoStatus::SocketError, error};
    }
    return IoStatus::Complete;
}
} // namespace network
