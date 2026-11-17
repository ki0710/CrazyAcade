#pragma once
#include "Board.h"

struct GridCell {
    int x, y; // x = column, y = row
};

inline GridCell PlayerStartCell(int stage, int playerId) {
    if (stage == 1) return {playerId == 1 ? 8 : 11, 18};
    if (stage == 2) return playerId == 1 ? GridCell{8, 5} : GridCell{11, 14};
    return {playerId == 1 ? 6 : 13, 5}; // Ice
}

inline bool IsMapCell(int x, int y) {
    return x >= 0 && x < MAP_TILES && y >= 0 && y < MAP_TILES;
}

inline bool CanMoveTo(int x, int y, const Board board[MAP_TILES][MAP_TILES]) {
    return IsMapCell(x, y) && board[y][x].obstacle == 0;
}
