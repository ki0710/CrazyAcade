#include "Packet.h"

namespace network {
namespace {
void Write32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value);
}
std::uint32_t Read32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
}

bool IsValidPacketType(PacketType type) {
    switch (type) {
    case PacketType::CS_HELLO:
    case PacketType::SC_WELCOME:
    case PacketType::SC_ROOM_STATE:
    case PacketType::SC_ERROR:
    case PacketType::SC_PEER_LEFT:
    case PacketType::SC_SNAPSHOT:
    case PacketType::SC_EVENT: return true;
    default: return false;
    }
}

bool IsValidHeader(const PacketHeader& header) {
    return header.packetSize >= kHeaderSize &&
           header.packetSize <= kMaxPacketSize && IsValidPacketType(header.packetType);
}

bool EncodePacket(PacketType type, std::uint32_t matchId,
                  const std::vector<std::uint8_t>& payload,
                  std::vector<std::uint8_t>& output) {
    if (!IsValidPacketType(type) || payload.size() > kMaxPacketSize - kHeaderSize)
        return false;
    std::vector<std::uint8_t> encoded(kHeaderSize + payload.size());
    Write32(encoded.data(), static_cast<std::uint32_t>(encoded.size()));
    const auto rawType = static_cast<std::uint16_t>(type);
    encoded[4] = static_cast<std::uint8_t>(rawType >> 8);
    encoded[5] = static_cast<std::uint8_t>(rawType);
    Write32(encoded.data() + 6, matchId);
    for (std::size_t i = 0; i < payload.size(); ++i)
        encoded[kHeaderSize + i] = payload[i];
    output.swap(encoded);
    return true;
}

bool DecodeHeader(const std::uint8_t* bytes, std::size_t size, PacketHeader& output) {
    if (!bytes || size < kHeaderSize) return false;
    PacketHeader header;
    header.packetSize = Read32(bytes);
    header.packetType = static_cast<PacketType>((std::uint16_t(bytes[4]) << 8) | bytes[5]);
    header.matchId = Read32(bytes + 6);
    if (!IsValidHeader(header)) return false;
    output = header;
    return true;
}

bool ReceiveBuffer::Append(const std::uint8_t* bytes, std::size_t size) {
    // At most one recv chunk may be added before extraction. The bound allows
    // the tail of a maximum-size packet plus coalesced bytes from the next one.
    if (invalid_) return false;
    if ((!bytes && size != 0) || size > kReceiveBufferSize ||
        bytes_.size() + size > kMaxPacketSize + kReceiveBufferSize) {
        invalid_ = true;
        return false;
    }
    if (size != 0) bytes_.insert(bytes_.end(), bytes, bytes + size);
    return true;
}

ExtractResult ReceiveBuffer::TryExtract(Packet& output) {
    if (invalid_) return ExtractResult::InvalidData;
    if (bytes_.size() < kHeaderSize) return ExtractResult::NeedMoreData;
    PacketHeader header;
    if (!DecodeHeader(bytes_.data(), bytes_.size(), header)) {
        invalid_ = true;
        return ExtractResult::InvalidData;
    }
    if (bytes_.size() < header.packetSize) return ExtractResult::NeedMoreData;
    output.header = header;
    output.payload.assign(bytes_.begin() + kHeaderSize, bytes_.begin() + header.packetSize);
    bytes_.erase(bytes_.begin(), bytes_.begin() + header.packetSize);
    return ExtractResult::PacketReady;
}

void ReceiveBuffer::Reset() {
    bytes_.clear();
    invalid_ = false;
}
} // namespace network
