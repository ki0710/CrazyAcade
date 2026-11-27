#include "MapItemSync.h"
#include "Protocol.h"
#include <cmath>

namespace network {
namespace {
constexpr std::uint8_t kObstacleBroken = 1;
constexpr std::uint8_t kItemCollected = 2;
constexpr std::uint8_t kItemDestroyed = 5;
constexpr std::size_t kCellSize = 4;
constexpr std::size_t kAbilitySize = 5;
constexpr std::size_t kEventSize = 4;
constexpr std::size_t kSnapshotSize = 1 + MAP_TILES * MAP_TILES * kCellSize + 2 * kAbilitySize;
constexpr std::size_t kIceStateSize = 3;
constexpr std::size_t kCityStateSize = 7;
// Two existing uint32 carried counts, two uint8 basket totals, and one uint8 winner.
constexpr std::size_t kForestStateSize = 11;

bool ValidCityState(const CityState& state) {
    if (state.trainStep < 0 || state.trainStep > kCityTrainTravelSteps) return false;
    for (int remaining : state.remaining)
        if (remaining < 0 || remaining > 65535) return false;

    switch (state.result) {
    case CityResult::Playing:    return state.remaining[0] > 0 && state.remaining[1] > 0;
    case CityResult::Player1Win: return state.remaining[0] > 0 && state.remaining[1] == 0;
    case CityResult::Player2Win: return state.remaining[0] == 0 && state.remaining[1] > 0;
    case CityResult::Draw:       return state.remaining[0] == 0 && state.remaining[1] == 0;
    default: return false;
    }
}

bool ReadCityState(const std::uint8_t* p, CityState& state) {
    CityState decoded;
    decoded.remaining[0] = PayloadWord(p);
    decoded.remaining[1] = PayloadWord(p + 2);
    decoded.result = static_cast<CityResult>(p[4]);
    decoded.trainStep = PayloadWord(p + 5);
    if (!ValidCityState(decoded)) return false;
    state = decoded;
    return true;
}

bool ApplyCityUpdate(const Packet& packet, CityState& state) {
    CityState decoded;
    if (packet.payload.size() != kCityStateSize || !ReadCityState(packet.payload.data(), decoded)) return false;
    if (packet.header.packetType == PacketType::SC_MATCH_END && decoded.result == CityResult::Playing) return false;
    if (state.result != CityResult::Playing &&
        (decoded.result != state.result || decoded.remaining[0] != state.remaining[0] ||
         decoded.remaining[1] != state.remaining[1] || decoded.trainStep != state.trainStep)) return false;
    state = decoded;
    return true;
}

bool ValidForestState(const ForestState& state) {
    for (int i = 0; i < 2; ++i) {
        if (state.carried[i] < 0 || state.carried[i] > kForestCarryLimit ||
            state.deposited[i] < 0 || state.deposited[i] >= kForestWinningTotal + kForestCarryLimit)
            return false;
    }
    if (state.winner < 0 || state.winner > 2) return false;
    if (state.winner) return state.deposited[state.winner - 1] >= kForestWinningTotal;
    return state.deposited[0] < kForestWinningTotal && state.deposited[1] < kForestWinningTotal;
}

bool ReadAppleCount(const std::uint8_t* p, int& carried) {
    const auto value = (std::uint32_t(PayloadWord(p)) << 16) | PayloadWord(p + 2);
    if (value > kForestCarryLimit) return false;
    carried = static_cast<int>(value);
    return true;
}

bool ReadForestState(const std::uint8_t* p, ForestState& state) {
    ForestState decoded;
    if (!ReadAppleCount(p, decoded.carried[0]) || !ReadAppleCount(p + 4, decoded.carried[1])) return false;
    decoded.deposited[0] = p[8];
    decoded.deposited[1] = p[9];
    decoded.winner = p[10];
    if (!ValidForestState(decoded)) return false;
    state = decoded;
    return true;
}

bool ApplyForestUpdate(const Packet& packet, ForestState& state) {
    ForestState decoded;
    if (packet.payload.size() != kForestStateSize || !ReadForestState(packet.payload.data(), decoded)) return false;
    if (packet.header.packetType == PacketType::SC_MATCH_END && !decoded.winner) return false;
    if (state.winner) {
        if (decoded.winner != state.winner) return false;
        for (int i = 0; i < 2; ++i)
            if (decoded.carried[i] != state.carried[i] || decoded.deposited[i] != state.deposited[i]) return false;
    }
    state = decoded;
    return true;
}

bool ValidIceState(const IceState& state) {
    for (int hearts : state.hearts) {
        if (hearts < 0 || hearts > 3) return false;
    }
    switch (state.result) {
    case IceResult::Playing: return state.hearts[0] > 0 && state.hearts[1] > 0;
    case IceResult::Player1Win: return state.hearts[0] > 0 && state.hearts[1] == 0;
    case IceResult::Player2Win: return state.hearts[0] == 0 && state.hearts[1] > 0;
    case IceResult::Draw: return state.hearts[0] == 0 && state.hearts[1] == 0;
    default: return false;
    }
}

bool ReadIceState(const std::uint8_t* p, IceState& state) {
    IceState decoded;
    decoded.hearts[0] = p[0];
    decoded.hearts[1] = p[1];
    decoded.result = static_cast<IceResult>(p[2]);
    if (!ValidIceState(decoded)) return false;
    state = decoded;
    return true;
}

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
    if (payload.empty() || payload[0] < 1 || payload[0] > 3) return false;
    const auto expected = kSnapshotSize + (payload[0] == 3 ? kIceStateSize :
        payload[0] == 1 ? kForestStateSize : kCityStateSize);
    if (payload.size() != expected) return false;
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
    if (snapshot.stage == 3 && !ReadIceState(p, snapshot.ice)) return false;
    if (snapshot.stage == 1 && !ReadForestState(p, snapshot.forest)) return false;
    if (snapshot.stage == 2 && !ReadCityState(p, snapshot.city)) return false;
    state = snapshot;
    return true;
}

bool ApplyIceUpdate(const Packet& packet, MapItemState& state) {
    if (state.stage != 3 || packet.payload.size() != kIceStateSize) return false;
    IceState decoded;
    if (!ReadIceState(packet.payload.data(), decoded)) return false;
    if (packet.header.packetType == PacketType::SC_MATCH_END &&
        decoded.result == IceResult::Playing) return false;
    if (state.ice.result != IceResult::Playing &&
        (decoded.result != state.ice.result ||
         decoded.hearts[0] != state.ice.hearts[0] ||
         decoded.hearts[1] != state.ice.hearts[1])) return false;
    state.ice = decoded;
    return true;
}

bool ApplyEvent(const std::vector<std::uint8_t>& payload, MapItemState& state, ItemPickup* pickup) {
    if (state.stage == 0 || payload.empty()) return false;
    if (payload[0] == static_cast<std::uint8_t>(ForestAppleEvent::Gained) ||
        payload[0] == static_cast<std::uint8_t>(ForestAppleEvent::Lost)) {
        if (state.stage != 1 || state.forest.winner || payload.size() != 6 ||
            payload[1] < 1 || payload[1] > 2) return false;
        int carried;
        if (!ReadAppleCount(payload.data() + 2, carried) ||
            ((payload[0] == static_cast<std::uint8_t>(ForestAppleEvent::Lost)) != (carried == 0))) return false;
        // Apply the server's total, rather than adding the event a second time.
        state.forest.carried[payload[1] - 1] = carried;
        return true;
    }
    if (payload.size() < kEventSize) return false;
    const int id = PayloadWord(payload.data() + 1);
    const int item = payload[3];
    if (id < 1 || id > MAP_TILES * MAP_TILES || item > (state.stage == 2 ? 4 : 3)) return false;
    Board& cell = state.board[(id - 1) / MAP_TILES][(id - 1) % MAP_TILES];

    if (payload[0] == kObstacleBroken) {
        if (payload.size() != kEventSize || (cell.obstacle != 1 && cell.obstacle != 0)) return false;
        if (cell.obstacle == 1) {
            cell.obstacle = 0;
            cell.item = item;
        }
        return true;
    }
    if (payload[0] == kItemDestroyed) {
        if (payload.size() != kEventSize || !item || (cell.obstacle != 0 && cell.obstacle != 9) ||
            (cell.item != 0 && cell.item != item)) return false;
        cell.item = 0;
        return true;
    }
    if (payload[0] != kItemCollected || payload.size() != kEventSize + 1 + kAbilitySize || item < 1 || item > 3)
        return false;

    const int playerId = payload[4];
    const ItemAbilities abilities = ReadAbilities(payload.data() + 5);
    if (playerId < 1 || playerId > 2 || !ValidAbilities(abilities)) return false;
    if (cell.item == 0) return true; // The pickup was already applied.
    if (cell.obstacle != 0 || cell.item != item) return false;
    cell.item = 0;
    state.abilities[playerId - 1] = abilities;
    if (pickup) *pickup = {playerId, item};
    return true;
}
}

std::vector<std::uint8_t> EncodeMapItemSnapshot(const MapItemState& state) {
    if (state.stage < 1 || state.stage > 3) return {};
    std::vector<std::uint8_t> payload;
    payload.reserve(kSnapshotSize + (state.stage == 3 ? kIceStateSize :
        state.stage == 1 ? kForestStateSize : kCityStateSize));
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
    if (state.stage == 3) {
        const auto ice = EncodeIceState(state.ice);
        if (ice.empty()) return {};
        payload.insert(payload.end(), ice.begin(), ice.end());
    }
    if (state.stage == 1) {
        const auto forest = EncodeForestState(state.forest);
        if (forest.empty()) return {};
        payload.insert(payload.end(), forest.begin(), forest.end());
    }
    if (state.stage == 2) {
        const auto city = EncodeCityState(state.city);
        if (city.empty()) return {};
        payload.insert(payload.end(), city.begin(), city.end());
    }
    return payload;
}

std::vector<std::uint8_t> EncodeCityState(const CityState& state) {
    if (!ValidCityState(state)) return {};
    std::vector<std::uint8_t> payload;
    for (int remaining : state.remaining) AppendWord(payload, remaining);
    payload.push_back(static_cast<std::uint8_t>(state.result));
    AppendWord(payload, state.trainStep);
    return payload;
}

std::vector<std::uint8_t> EncodeForestState(const ForestState& state) {
    if (!ValidForestState(state)) return {};
    std::vector<std::uint8_t> payload;
    for (int carried : state.carried) {
        AppendWord(payload, carried >> 16);
        AppendWord(payload, carried);
    }
    payload.push_back(static_cast<std::uint8_t>(state.deposited[0]));
    payload.push_back(static_cast<std::uint8_t>(state.deposited[1]));
    payload.push_back(static_cast<std::uint8_t>(state.winner));
    return payload;
}

std::vector<std::uint8_t> EncodeForestAppleEvent(ForestAppleEvent kind, int playerId, int carried) {
    if (playerId < 1 || playerId > 2 || carried < 0 || carried > kForestCarryLimit ||
        (kind != ForestAppleEvent::Gained && kind != ForestAppleEvent::Lost) ||
        ((kind == ForestAppleEvent::Lost) != (carried == 0))) return {};
    std::vector<std::uint8_t> payload = {static_cast<std::uint8_t>(kind), static_cast<std::uint8_t>(playerId)};
    AppendWord(payload, carried >> 16);
    AppendWord(payload, carried);
    return payload;
}

std::vector<std::uint8_t> EncodeIceState(const IceState& state) {
    if (!ValidIceState(state)) return {};
    return {static_cast<std::uint8_t>(state.hearts[0]),
            static_cast<std::uint8_t>(state.hearts[1]),
            static_cast<std::uint8_t>(state.result)};
}

std::vector<std::uint8_t> EncodeObstacleBroken(int x, int y, const Board& cell) {
    if (cell.obstacle != 0 || cell.item < 0 || cell.item > 3) return {};
    return CellEvent(kObstacleBroken, x, y, cell.item);
}

std::vector<std::uint8_t> EncodeItemDestroyed(int x, int y, int item) {
    if (item < 1 || item > 4) return {};
    return CellEvent(kItemDestroyed, x, y, item);
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

bool ApplyMapItemPacket(const Packet& packet, MapItemState& state, ItemPickup* pickup) {
    if (pickup) *pickup = {};
    if (packet.header.packetType == PacketType::SC_SNAPSHOT) return ApplySnapshot(packet.payload, state);
    if (packet.header.packetType == PacketType::SC_EVENT) return ApplyEvent(packet.payload, state, pickup);
    if (packet.header.packetType == PacketType::SC_STATE || packet.header.packetType == PacketType::SC_MATCH_END) {
        if (state.stage == 1) return ApplyForestUpdate(packet, state.forest);
        if (state.stage == 2) return ApplyCityUpdate(packet, state.city);
        return ApplyIceUpdate(packet, state);
    }
    return false;
}

} // namespace network
