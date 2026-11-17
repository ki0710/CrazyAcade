#pragma once
#include "Protocol.h"
#include "../../CrazyArcade/MovementRules.h"

namespace network {
constexpr int kMoveTickMs = 20;
constexpr int kCellUnits = 1200;
constexpr int kBaseStepUnits = kMoveTickMs * kCellUnits / (12 * 8); // 250, local base speed

struct PlayerPosition {
    GridCell cell{};
    GridCell target{};
    int progress = 0; // In [0, kCellUnits); cell is the last completed tile.
    std::uint8_t facing = kInputDown;
};

struct MovingPlayer {
    PlayerPosition position{};
    std::uint32_t lastSequence = 0;
    std::uint8_t direction = kInputNone;

    bool AcceptInput(const InputData& input) {
        if (input.sequence <= lastSequence) return false;
        lastSequence = input.sequence;
        direction = input.directionMask;
        return true;
    }

    bool Step(const Board board[MAP_TILES][MAP_TILES], float speed) {
        auto& p = position;
        if (p.progress == 0) {
            auto target = p.cell;
            switch (direction) {
            case kInputUp:    --target.y; break;
            case kInputDown:  ++target.y; break;
            case kInputLeft:  --target.x; break;
            case kInputRight: ++target.x; break;
            default: return false;
            }
            if (!CanMoveTo(target.x, target.y, board)) return false;
            p.target = target;
            p.facing = direction;
        }
        p.progress += static_cast<int>(speed * kBaseStepUnits + 0.5f);
        if (p.progress >= kCellUnits) {
            p.cell = p.target;
            p.progress = 0; // Finish this tile before using a different direction.
        }
        return true;
    }
};

struct PlayerFrame {
    std::uint32_t tick = 0;
    PlayerPosition players[2]; // Fixed P1, P2 order.
};

constexpr std::size_t kPlayerStateSize = 4 + 2 * 7;
inline std::vector<std::uint8_t> EncodePlayerState(const PlayerFrame& frame) {
    const auto t = frame.tick;
    std::vector<std::uint8_t> bytes = {std::uint8_t(t >> 24), std::uint8_t(t >> 16),
        std::uint8_t(t >> 8), std::uint8_t(t)};
    for (const auto& p : frame.players) {
        bytes.insert(bytes.end(), {std::uint8_t(p.cell.x), std::uint8_t(p.cell.y),
            std::uint8_t(p.target.x), std::uint8_t(p.target.y),
            std::uint8_t(p.progress >> 8), std::uint8_t(p.progress), p.facing});
    }
    return bytes;
}

inline bool ReadPlayerState(const std::vector<std::uint8_t>& bytes, PlayerFrame& output) {
    if (bytes.size() != kPlayerStateSize) return false;
    PlayerFrame frame;
    frame.tick = (std::uint32_t(PayloadWord(bytes.data())) << 16) | PayloadWord(bytes.data() + 2);
    if (frame.tick == 0) return false;
    const auto* fields = bytes.data() + 4;
    for (auto& p : frame.players) {
        p = {{fields[0], fields[1]}, {fields[2], fields[3]}, PayloadWord(fields + 4), fields[6]};
        fields += 7;
        if (!IsMapCell(p.cell.x, p.cell.y) || !IsMapCell(p.target.x, p.target.y) ||
            p.progress >= kCellUnits || p.facing == kInputNone || !InputDirectionText(p.facing)) return false;
        const int dx = p.target.x - p.cell.x, dy = p.target.y - p.cell.y;
        if (p.progress == 0) {
            if (dx != 0 || dy != 0) return false;
        } else if (dx != (p.facing == kInputRight ? 1 : p.facing == kInputLeft ? -1 : 0) ||
                   dy != (p.facing == kInputDown ? 1 : p.facing == kInputUp ? -1 : 0)) return false;
    }
    output = frame;
    return true;
}
} // namespace network
