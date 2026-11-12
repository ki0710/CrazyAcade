#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace network {

constexpr std::size_t kHeaderSize = 10;
constexpr std::size_t kReceiveBufferSize = 4096;
// Protocol limit, independent of the size of a single recv().
constexpr std::uint32_t kMaxPacketSize = 1024 * 1024;

enum class PacketType : std::uint16_t {
    CS_HELLO = 1,
    SC_WELCOME = 2,
    SC_ROOM_STATE = 3,
    SC_ERROR = 4,
    SC_PEER_LEFT = 5,
    SC_SNAPSHOT = 6,
    SC_EVENT = 7,
    CS_INPUT = 8
};

// Host-order values only. Never transmit sizeof(PacketHeader) bytes.
struct PacketHeader {
    std::uint32_t packetSize = static_cast<std::uint32_t>(kHeaderSize);
    PacketType packetType = PacketType::CS_HELLO;
    std::uint32_t matchId = 0;
};

struct Packet {
    PacketHeader header;
    std::vector<std::uint8_t> payload;
};

bool IsValidPacketType(PacketType type);
bool IsValidHeader(const PacketHeader& header);
// Outputs are unchanged on failure. Payload bytes are opaque; future payload
// integer fields must also be explicitly encoded in network byte order.
bool EncodePacket(PacketType type, std::uint32_t matchId,
                  const std::vector<std::uint8_t>& payload,
                  std::vector<std::uint8_t>& output);
bool DecodeHeader(const std::uint8_t* bytes, std::size_t size,
                  PacketHeader& output);

enum class ExtractResult { PacketReady, NeedMoreData, InvalidData };

// One instance per connection. Drain TryExtract after each Append.
// Invalid input poisons the stream until Reset; do not attempt resynchronization.
class ReceiveBuffer {
public:
    bool Append(const std::uint8_t* bytes, std::size_t size);
    ExtractResult TryExtract(Packet& output);
    bool HasIncompletePacket() const { return !bytes_.empty(); }
    void Reset();
private:
    std::vector<std::uint8_t> bytes_;
    bool invalid_ = false;
};

} // namespace network
