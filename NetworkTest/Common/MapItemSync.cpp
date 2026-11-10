#include "MapItemSync.h"
#include "Protocol.h"
#include <cmath>

namespace network {
namespace {
constexpr std::uint8_t kObstacleBroken = 1;
constexpr std::uint8_t kItemCollected = 2;
constexpr std::size_t kCellSize = 4;
constexpr std::size_t kAbilitySize = 5;
constexpr std::size_t kEventSize = 4;
constexpr std::size_t kSnapshotSize = 1 + MAP_TILES * MAP_TILES * kCellSize + 2 * kAbilitySize;

bool ValidCell(const Board& cell, int stage) {
    const int maxObstacle = stage == 1 ? 3 : (stage == 2 ? 6 : 2);
    if (cell.obstacle < 0 || (cell.obstacle > maxObstacle && cell.obstacle != 9) ||
        cell.item < 0 || cell.item > (stage == 2 ? 4 : 3) ||
        cell.obstacle_tpye < 0 || cell.obstacle_tpye > 5) return false;

    if (stage == 1) {
        return cell.tile >= 1 && cell.tile <= 6 &&
            (cell.obstacle != 1 || cell.obstacle_tpye >= 1);
    }
    return cell.tile == 0 && (stage != 3 || cell.obstacle_tpye <= 1);
}

bool ValidAbilities(const ItemAbilities& abilities) {
    return abilities.blastRange >= 1 && abilities.blastRange <= 65535 &&
        abilities.bombLimit >= 1 && abilities.bombLimit <= 65535 &&
        abilities.speed >= 1.0f && abilities.speed <= 1.5f;
}

void AppendWord(std::vector<std::uint8_t>& payload, int value) {
    payload.push_back(static_cast<std::uint8_t>(value >> 8));
    payload.push_back(static_cast<std::uint8_t>(value));
}

void AppendAbilities(std::vector<std::uint8_t>& payload, const ItemAbilities& abilities) {
    AppendWord(payload, abilities.blastRange);
    AppendWord(payload, abilities.bombLimit);
    payload.push_back(static_cast<std::uint8_t>(std::lround(abilities.speed * 10.0f)));
}

ItemAbilities ReadAbilities(const std::uint8_t* p) {
    return {PayloadWord(p), PayloadWord(p + 2), p[4] / 10.0f};
}

std::vector<std::uint8_t> CellEvent(std::uint8_t kind, int x, int y, int item) {
    if (x < 0 || x >= MAP_TILES || y < 0 || y >= MAP_TILES) return {};
    std::vector<std::uint8_t> payload = {kind};
    AppendWord(payload, y * MAP_TILES + x + 1);
    payload.push_back(static_cast<std::uint8_t>(item));
    return payload;
}

bool ApplySnapshot(const std::vector<std::uint8_t>& payload, MapItemState& state) {
    if (payload.size() != kSnapshotSize || payload[0] < 1 || payload[0] > 3) return false;
    MapItemState snapshot;
    snapshot.stage = payload[0];
    const std::uint8_t* p = payload.data() + 1;
    for (auto& row : snapshot.board) {
        for (auto& cell : row) {
            cell = {p[0], p[1], p[2], p[3]};
            if (!ValidCell(cell, snapshot.stage)) return false;
            p += kCellSize;
        }
    }
    for (auto& abilities : snapshot.abilities) {
        abilities = ReadAbilities(p);
        if (!ValidAbilities(abilities)) return false;
        p += kAbilitySize;
    }
    state = snapshot;
    return true;
}

bool ApplyEvent(const std::vector<std::uint8_t>& payload, MapItemState& state) {
    if (state.stage == 0 || payload.size() < kEventSize) return false;
    const int id = PayloadWord(payload.data() + 1);
    const int item = payload[3];
    if (id < 1 || id > MAP_TILES * MAP_TILES || item > 3) return false;
    Board& cell = state.board[(id - 1) / MAP_TILES][(id - 1) % MAP_TILES];

    if (payload[0] == kObstacleBroken) {
        if (payload.size() != kEventSize || (cell.obstacle != 1 && cell.obstacle != 0)) return false;
        if (cell.obstacle == 1) {
            cell.obstacle = 0;
            cell.item = item;
        }
        return true;
    }
    if (payload[0] != kItemCollected || payload.size() != kEventSize + 1 + kAbilitySize ||
        item == 0 || cell.obstacle != 0 || (cell.item != item && cell.item != 0)) return false;

    const int playerId = payload[4];
    const ItemAbilities abilities = ReadAbilities(payload.data() + 5);
    if (playerId < 1 || playerId > 2 || !ValidAbilities(abilities)) return false;
    if (cell.item == 0) return true; // The pickup was already applied.
    cell.item = 0;
    state.abilities[playerId - 1] = abilities;
    return true;
}
}

std::vector<std::uint8_t> EncodeMapItemSnapshot(const MapItemState& state) {
    if (state.stage < 1 || state.stage > 3) return {};
    std::vector<std::uint8_t> payload;
    payload.reserve(kSnapshotSize);
    payload.push_back(static_cast<std::uint8_t>(state.stage));
    for (const auto& row : state.board) {
        for (const auto& cell : row) {
            if (!ValidCell(cell, state.stage)) return {};
            payload.push_back(static_cast<std::uint8_t>(cell.obstacle));
            payload.push_back(static_cast<std::uint8_t>(cell.obstacle_tpye));
            payload.push_back(static_cast<std::uint8_t>(cell.tile));
            payload.push_back(static_cast<std::uint8_t>(cell.item));
        }
    }
    for (const auto& abilities : state.abilities) {
        if (!ValidAbilities(abilities)) return {};
        AppendAbilities(payload, abilities);
    }
    return payload;
}

std::vector<std::uint8_t> EncodeObstacleBroken(int x, int y, const Board& cell) {
    if (cell.obstacle != 0 || cell.item < 0 || cell.item > 3) return {};
    return CellEvent(kObstacleBroken, x, y, cell.item);
}

std::vector<std::uint8_t> EncodeItemCollected(
    int x, int y, int item, int playerId, const ItemAbilities& abilities) {
    if (item < 1 || item > 3 || playerId < 1 || playerId > 2 || !ValidAbilities(abilities)) return {};
    auto payload = CellEvent(kItemCollected, x, y, item);
    if (payload.empty()) return payload;
    payload.push_back(static_cast<std::uint8_t>(playerId));
    AppendAbilities(payload, abilities);
    return payload;
}

bool ApplyMapItemPacket(const Packet& packet, MapItemState& state) {
    if (packet.header.packetType == PacketType::SC_SNAPSHOT) return ApplySnapshot(packet.payload, state);
    if (packet.header.packetType == PacketType::SC_EVENT) return ApplyEvent(packet.payload, state);
    return false;
}

} // namespace network
