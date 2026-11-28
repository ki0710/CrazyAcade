#pragma once

#include "Packet.h"
#include "../../CrazyArcade/Board.h"
#include "../../CrazyArcade/IceState.h"
#include "../../CrazyArcade/ForestState.h"
#include "../../CrazyArcade/CityState.h"

namespace network {

struct MapItemState {
    int stage = 0;
    Board board[MAP_TILES][MAP_TILES] = {};
    ItemAbilities abilities[2];
    IceState ice;
    ForestState forest;
    CityState city;
};

// Returns an empty payload when the source state cannot be encoded.
std::vector<std::uint8_t> EncodeMapItemSnapshot(const MapItemState& state);
std::vector<std::uint8_t> EncodeObstacleBroken(int x, int y, const Board& cell);
std::vector<std::uint8_t> EncodeCityItemLanded(int x, int y, int item);
std::vector<std::uint8_t> EncodeItemDestroyed(int x, int y, int item);
std::vector<std::uint8_t> EncodeItemCollected(
    int x, int y, int item, int playerId, const ItemAbilities& abilities);

std::vector<std::uint8_t> EncodeIceState(const IceState& state);
std::vector<std::uint8_t> EncodeForestState(const ForestState& state);
std::vector<std::uint8_t> EncodeCityState(const CityState& state);
enum class ForestAppleEvent : std::uint8_t { Gained = 3, Lost = 4 };
std::vector<std::uint8_t> EncodeForestAppleEvent(ForestAppleEvent kind, int playerId, int carried);

constexpr std::uint8_t kCityHitEventKind = 7;
enum class CityHitCause : std::uint8_t { Train = 1, Bomb = 2 };
struct CityHitEvent {
    int playerId;
    CityHitCause cause;
    std::uint32_t sequence;
};
std::vector<std::uint8_t> EncodeCityHitEvent(const CityHitEvent& hit);
bool ReadCityHitEvent(const std::vector<std::uint8_t>& payload, CityHitEvent& hit);

struct ItemPickup {
    int playerId = 0;
    int item = 0;
};

// The session checks matchId and packet direction before applying the payload.
// pickup is populated only when the event actually removes an item.
bool ApplyMapItemPacket(const Packet& packet, MapItemState& state, ItemPickup* pickup = nullptr);

} // namespace network
