#pragma once
#include "PlayerMovement.h"
#include <algorithm>

namespace network {
constexpr std::uint64_t kBombFuseMs = 1050;
constexpr std::uint64_t kBombBlastMs = 550;

struct ServerBomb {
    std::uint32_t id = 0;
    int owner = 0, col = 0, row = 0, state = 1, range = 1;
    int left = 1, right = 1, up = 1, down = 1;
    std::uint64_t explodeAt = 0, removeAt = 0; // Server monotonic milliseconds.
    bool hit[2] = {false, false};
};

inline bool TryPlaceBomb(std::vector<ServerBomb>& bombs, std::uint32_t& nextId,
                         int owner, const MovingPlayer& player, const ItemAbilities& ability,
                         Board board[MAP_TILES][MAP_TILES], std::uint64_t now) {
    if (owner < 1 || owner > 2 || !nextId || ability.blastRange < 1 || ability.blastRange > 65535)
        return false;
    const auto cell = player.position.cell;
    const auto count = std::count_if(bombs.begin(), bombs.end(),
        [owner](const ServerBomb& b) { return b.owner == owner; });
    if (count >= ability.bombLimit || !CanMoveTo(cell.x, cell.y, board)) return false;
    for (const auto& b : bombs)
        if (b.state == 1 && b.col == cell.x && b.row == cell.y) return false;
    ServerBomb b;
    b.id = nextId++;
    b.owner = owner; b.col = cell.x; b.row = cell.y;
    b.range = b.left = b.right = b.up = b.down = ability.blastRange;
    b.explodeAt = now + kBombFuseMs;
    b.removeAt = b.explodeAt + kBombBlastMs;
    bombs.push_back(b);
    board[cell.y][cell.x].obstacle = 9;
    return true;
}

inline void ReleaseBombCell(const ServerBomb& bomb, const std::vector<ServerBomb>& bombs,
                            Board board[MAP_TILES][MAP_TILES]) {
    for (const auto& other : bombs)
        if (other.id != bomb.id && other.state == 1 && other.col == bomb.col && other.row == bomb.row)
            return;
    if (board[bomb.row][bomb.col].obstacle == 9) board[bomb.row][bomb.col].obstacle = 0;
}

struct BombChanges {
    std::vector<GridCell> broken;
    std::vector<int> treeOwners;
    std::vector<int> hits; // Repeated ID means hits by different bombs.
};

inline bool BombHitsCell(const ServerBomb& b, int col, int row) {
    return (row == b.row && col >= b.col - b.left && col <= b.col + b.right) ||
           (col == b.col && row >= b.row - b.up && row <= b.row + b.down);
}

inline GridCell PlayerHitCell(const PlayerPosition& p) {
    const float fraction = p.progress / static_cast<float>(kCellUnits);
    return {static_cast<int>(p.cell.x + (p.target.x - p.cell.x) * fraction + 0.5f),
            static_cast<int>(p.cell.y + (p.target.y - p.cell.y) * fraction + 0.5f)};
}

inline void ComputeBombRange(ServerBomb& b, Board board[MAP_TILES][MAP_TILES],
                             bool forest, BombChanges& changes) {
    const int dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1};
    int* reach[] = {&b.left, &b.right, &b.up, &b.down};
    for (int dir = 0; dir < 4; ++dir) {
        for (int step = 1; step <= b.range; ++step) {
            const int col = b.col + dx[dir] * step, row = b.row + dy[dir] * step;
            if (!IsMapCell(col, row)) break;
            auto& cell = board[row][col];
            if (cell.obstacle == 1) {
                BreakBoardObstacle(cell);
                *reach[dir] = step;
                changes.broken.push_back({col, row});
                break;
            }
            if (cell.obstacle == 2) {
                *reach[dir] = step;
                if (forest) changes.treeOwners.push_back(b.owner);
                break;
            }
            // 9 passes without chain reactions; 3..6 keep the local pass-through rule.
        }
    }
}

inline BombChanges UpdateBombs(std::vector<ServerBomb>& bombs, Board board[MAP_TILES][MAP_TILES],
                              const MovingPlayer players[2], std::uint64_t now,
                              bool allowDamage, bool forest) {
    BombChanges changes;
    for (auto& b : bombs) {
        if (allowDamage && b.state == 1 && now >= b.explodeAt) {
            b.state = 2; // A state transition occurs once, independent of paint calls.
            ReleaseBombCell(b, bombs, board);
            ComputeBombRange(b, board, forest, changes);
        }
    }
    for (auto& b : bombs) {
        if (!allowDamage || b.state != 2 || now >= b.removeAt) continue;
        for (int i = 0; i < 2; ++i) {
            const auto cell = PlayerHitCell(players[i].position);
            if (!b.hit[i] && BombHitsCell(b, cell.x, cell.y)) {
                b.hit[i] = true;
                changes.hits.push_back(i + 1);
            }
        }
    }
    bombs.erase(std::remove_if(bombs.begin(), bombs.end(), [&](const ServerBomb& b) {
        if (b.state != 2 || now < b.removeAt) return false;
        ReleaseBombCell(b, bombs, board);
        return true;
    }), bombs.end());
    return changes;
}

// At match end cancel planted bombs, but let already visible blasts expire without damage.
inline void CancelPlantedBombs(std::vector<ServerBomb>& bombs, Board board[MAP_TILES][MAP_TILES]) {
    for (auto& b : bombs) if (b.state == 1) b.state = 0;
    for (const auto& b : bombs) if (b.state == 0) ReleaseBombCell(b, bombs, board);
    bombs.erase(std::remove_if(bombs.begin(), bombs.end(),
        [](const ServerBomb& b) { return b.state == 0; }), bombs.end());
}
} // namespace network
