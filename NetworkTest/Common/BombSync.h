#pragma once
#include "BombRules.h"
#include <set>

namespace network {
struct BombView {
    std::uint32_t id;
    int owner, col, row, state, left, right, up, down, time;
};
struct BombFrame {
    std::uint32_t tick = 0, sequence = 0;
    std::vector<BombView> bombs;
};
constexpr std::size_t kBombViewSize = 17;
inline void BombWrite32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(std::uint8_t(value >> shift));
}
inline std::uint32_t BombRead32(const std::uint8_t* p) {
    return (std::uint32_t(PayloadWord(p)) << 16) | PayloadWord(p + 2);
}
inline bool ValidBombView(const BombView& b) {
    return b.id && b.owner >= 1 && b.owner <= 2 && IsMapCell(b.col, b.row) &&
        (b.state == 1 || b.state == 2) &&
        b.left >= 0 && b.left <= 65535 && b.right >= 0 && b.right <= 65535 &&
        b.up >= 0 && b.up <= 65535 && b.down >= 0 && b.down <= 65535 &&
        b.time >= 0 && b.time <= (b.state == 1 ? 20 : 10);
}
inline std::vector<std::uint8_t> EncodeBombState(const BombFrame& frame) {
    if (!frame.tick || !frame.sequence || frame.bombs.size() > 65535 ||
        10 + frame.bombs.size() * kBombViewSize > kMaxPacketSize - kHeaderSize) return {};
    std::vector<std::uint8_t> bytes;
    BombWrite32(bytes, frame.tick);
    BombWrite32(bytes, frame.sequence);
    bytes.push_back(std::uint8_t(frame.bombs.size() >> 8));
    bytes.push_back(std::uint8_t(frame.bombs.size()));
    std::set<std::uint32_t> ids;
    for (const auto& b : frame.bombs) {
        if (!ValidBombView(b) || !ids.insert(b.id).second) return {};
        BombWrite32(bytes, b.id);
        bytes.insert(bytes.end(), {std::uint8_t(b.owner), std::uint8_t(b.col), std::uint8_t(b.row),
                                  std::uint8_t(b.state)});
        for (int reach : {b.left, b.right, b.up, b.down}) {
            bytes.push_back(std::uint8_t(reach >> 8));
            bytes.push_back(std::uint8_t(reach));
        }
        bytes.push_back(std::uint8_t(b.time));
    }
    return bytes;
}
inline bool ReadBombState(const std::vector<std::uint8_t>& bytes, BombFrame& output) {
    if (bytes.size() < 10 || bytes.size() > kMaxPacketSize - kHeaderSize) return false;
    const int count = PayloadWord(bytes.data() + 8);
    if (bytes.size() != 10 + count * kBombViewSize) return false;
    BombFrame frame;
    frame.tick = BombRead32(bytes.data());
    frame.sequence = BombRead32(bytes.data() + 4);
    if (!frame.tick || !frame.sequence) return false;
    std::set<std::uint32_t> ids;
    for (int i = 0; i < count; ++i) {
        const auto* p = bytes.data() + 10 + i * kBombViewSize;
        BombView b{BombRead32(p), p[4], p[5], p[6], p[7],
                   PayloadWord(p + 8), PayloadWord(p + 10), PayloadWord(p + 12), PayloadWord(p + 14), p[16]};
        if (!ValidBombView(b) || !ids.insert(b.id).second) return false;
        frame.bombs.push_back(b);
    }
    output = std::move(frame);
    return true;
}
inline BombView ViewBomb(const ServerBomb& b, std::uint64_t now) {
    const auto end = b.state == 1 ? b.explodeAt : b.removeAt;
    const int remaining = end > now ? static_cast<int>((end - now + 49) / 50) - 1 : 0;
    return {b.id, b.owner, b.col, b.row, b.state, b.left, b.right, b.up, b.down,
            (std::max)(0, (std::min)(b.state == 1 ? 20 : 10, remaining))};
}
// Apply only Bomb occupancy, retaining preassigned items and unrelated obstacles.
inline void ApplyBombOccupancy(Board board[MAP_TILES][MAP_TILES], const BombFrame& frame) {
    for (int row = 0; row < MAP_TILES; ++row)
        for (int col = 0; col < MAP_TILES; ++col)
            if (board[row][col].obstacle == 9) board[row][col].obstacle = 0;
    for (const auto& b : frame.bombs)
        if (b.state == 1) board[b.row][b.col].obstacle = 9;
}
} // namespace network
