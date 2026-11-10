#pragma once

#include "Packet.h"
#include "../../CrazyArcade/Board.h"

namespace network {

struct MapItemState {
    int stage = 0;
    Board board[MAP_TILES][MAP_TILES] = {};
    ItemAbilities abilities[2];
};

// Returns an empty payload when the source state cannot be encoded.
std::vector<std::uint8_t> EncodeMapItemSnapshot(const MapItemState& state);
std::vector<std::uint8_t> EncodeObstacleBroken(int x, int y, const Board& cell);
std::vector<std::uint8_t> EncodeItemCollected(
    int x, int y, int item, int playerId, const ItemAbilities& abilities);

// The session checks matchId and packet direction before applying the payload.
bool ApplyMapItemPacket(const Packet& packet, MapItemState& state);

} // namespace network
