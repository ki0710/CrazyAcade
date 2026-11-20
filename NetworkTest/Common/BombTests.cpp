// Deterministic tests for the production server rules/codecs; no sockets or renderer.
#include "BombSync.h"
#include "MapItemSync.h"
#include <cassert>
#include <iostream>
using namespace network;
static int checks = 0;
static void Check(bool value) { ++checks; assert(value); }

static MovingPlayer At(int col, int row) {
    MovingPlayer player;
    player.position.cell = player.position.target = {col, row};
    return player;
}

int main() {
    int apples = 2;
    Check(CollectForestApple(apples) && apples == 3);
    Check(!CollectForestApple(apples) && apples == 3);
    Check(LoseForestApples(apples) && CollectForestApple(apples) && apples == 1);
    Board board[MAP_TILES][MAP_TILES]{};
    MovingPlayer players[2] = {At(7, 11), At(13, 5)};
    ItemAbilities ability;
    std::vector<ServerBomb> bombs;
    std::uint32_t next = 1;
    board[11][7].item = 3;
    Check(TryPlaceBomb(bombs, next, 1, players[0], ability, board, 100));
    Check(bombs[0].id == 1 && bombs[0].owner == 1 && bombs[0].row == 11 && bombs[0].col == 7);
    Check(board[11][7].obstacle == 9 && board[11][7].item == 3 && board[7][11].obstacle == 0);
    Check(!TryPlaceBomb(bombs, next, 2, players[0], ability, board, 100));
    Check(!TryPlaceBomb(bombs, next, 1, players[0], ability, board, 100));
    players[0].direction = kInputRight;
    for (int i = 0; i < 5; ++i) Check(players[0].Step(board, 1.0f));
    Check(players[0].position.cell.x == 8); // Escape a Bomb.
    players[0].direction = kInputLeft;
    Check(!players[0].Step(board, 1.0f)); // Cannot re-enter.
    Check(TryPlaceBomb(bombs, next, 1, players[0], ability, board, 400));
    Check(bombs[1].id == 2);
    players[0] = At(9, 11);
    Check(!TryPlaceBomb(bombs, next, 1, players[0], ability, board, 400));
    Check(TryPlaceBomb(bombs, next, 2, players[1], ability, board, 500));
    Check(UpdateBombs(bombs, board, players, 1149, true, false).hits.empty());
    Check(bombs[0].state == 1 && board[11][7].obstacle == 9);
    const auto hit = UpdateBombs(bombs, board, players, 1150, true, false);
    Check(bombs[0].state == 2 && bombs[1].state == 1 && bombs[2].state == 1);
    Check(board[11][7].obstacle == 0 && board[11][8].obstacle == 9);
    Check(bombs[1].explodeAt == 1450 && bombs[2].explodeAt == 1550);
    UpdateBombs(bombs, board, players, 1699, true, false);
    Check(bombs.size() == 3);
    UpdateBombs(bombs, board, players, 1700, true, false);
    Check(bombs.size() == 2 && bombs[0].id == 2 && bombs[1].id == 3);
    players[0] = At(9, 11);
    Check(TryPlaceBomb(bombs, next, 1, players[0], ability, board, 1700)); // Restored slot.
    Check(bombs.back().id == 4);

    // Same-cell replacement while an old explosion still exists.
    bombs.clear(); board[11][7] = {};
    players[0] = At(7, 11);
    Check(TryPlaceBomb(bombs, next, 1, players[0], ability, board, 2000));
    UpdateBombs(bombs, board, players, 3050, true, false);
    Check(TryPlaceBomb(bombs, next, 1, players[0], ability, board, 3100));
    UpdateBombs(bombs, board, players, 3600, true, false);
    Check(bombs.size() == 1 && bombs[0].state == 1 && board[11][7].obstacle == 9);
    CancelPlantedBombs(bombs, board);
    Check(bombs.empty() && board[11][7].obstacle == 0);

    for (int obstacle : {1, 2, 3, 4, 5, 6, 9}) {
        board[11][7].obstacle = obstacle;
        Check(!TryPlaceBomb(bombs, next, 1, players[0], ability, board, 0));
        Check(board[11][7].obstacle == obstacle);
    }
    for (GridCell cell : {GridCell{-1, 5}, GridCell{5, -1}, GridCell{20, 5}, GridCell{5, 20}})
        Check(!TryPlaceBomb(bombs, next, 1, At(cell.x, cell.y), ability, board, 0));

    // Independent ranges, exact endpoints, and preserved hidden item.
    for (auto& row : board) for (auto& cell : row) cell = {};
    players[0] = At(7, 11);
    ability.blastRange = 6;
    board[11][6] = {1, 0, 0, 1};
    board[11][9].obstacle = board[8][7].obstacle = board[15][7].obstacle = 1;
    board[11][5].obstacle = 1;
    Check(TryPlaceBomb(bombs, next, 1, players[0], ability, board, 0));
    const auto changes = UpdateBombs(bombs, board, players, 1050, true, false);
    const auto& b = bombs[0];
    Check(b.left == 1 && b.right == 2 && b.up == 3 && b.down == 4);
    Check(changes.broken.size() == 4 && board[11][6].obstacle == 0 && board[11][6].item == 1);
    Check(board[11][5].obstacle == 1);
    for (int row = 0; row < MAP_TILES; ++row) {
        for (int col = 0; col < MAP_TILES; ++col) {
            const bool expected = (row == 11 && col >= 6 && col <= 9) ||
                                  (col == 7 && row >= 8 && row <= 15);
            Check(BombHitsCell(b, col, row) == expected);
        }
    }
    Check(UpdateBombs(bombs, board, players, 1070, true, false).hits.empty());

    // Entry into an existing blast uses display position, then deduplicates damage.
    players[1] = At(5, 11);
    players[1].position.target = {6, 11};
    players[1].position.facing = kInputRight;
    players[1].position.progress = 599;
    Check(UpdateBombs(bombs, board, players, 1090, true, false).hits.empty());
    players[1].position.progress = 600;
    auto entry = UpdateBombs(bombs, board, players, 1110, true, false);
    Check(entry.hits.size() == 1 && entry.hits[0] == 2);
    Check(UpdateBombs(bombs, board, players, 1130, true, false).hits.empty());

    // Obstacle 2 stops without destruction; 9 and 3..6 pass; no chain reactions.
    for (int obstacle : {2, 3, 4, 5, 6, 9}) {
        for (bool forest : {false, true}) {
            bombs.clear();
            for (auto& row : board) for (auto& cell : row) cell = {};
            board[11][8].obstacle = obstacle;
            Check(TryPlaceBomb(bombs, next, 1, At(7, 11), ability, board, 0));
            auto events = UpdateBombs(bombs, board, players, 1050, true, forest);
            Check(board[11][8].obstacle == obstacle);
            Check(bombs[0].right == (obstacle == 2 ? 1 : 6));
            Check(events.treeOwners.size() == (obstacle == 2 && forest ? 1u : 0u));
        }
    }
    for (GridCell corner : {GridCell{0, 0}, GridCell{19, 19}}) {
        bombs.clear();
        for (auto& row : board) for (auto& cell : row) cell = {};
        ability.blastRange = 65535;
        Check(TryPlaceBomb(bombs, next, 1, At(corner.x, corner.y), ability, board, 0));
        UpdateBombs(bombs, board, players, 5000, true, false); // Both deadlines elapsed.
        Check(bombs.empty() && board[corner.y][corner.x].obstacle == 0);
    }
    // Two separate bombs can each hit both players, including owners.
    bombs.clear();
    for (auto& row : board) for (auto& cell : row) cell = {};
    players[0] = At(7, 11); players[1] = At(8, 11); ability = ItemAbilities{};
    Check(TryPlaceBomb(bombs, next, 1, players[0], ability, board, 0));
    Check(TryPlaceBomb(bombs, next, 2, players[1], ability, board, 0));
    auto both = UpdateBombs(bombs, board, players, 1050, true, false);
    Check(both.hits.size() == 4 && bombs[0].hit[0] && bombs[0].hit[1] &&
          bombs[1].hit[0] && bombs[1].hit[1]);
    IceState ice;
    ice.hearts[0] = ice.hearts[1] = 2;
    for (int id : both.hits) ApplyIceHit(ice, id);
    Check(FinishIceTick(ice) && ice.result == IceResult::Draw);
    Check(!ApplyIceHit(ice, 1) && !FinishIceTick(ice));
    Check(UpdateBombs(bombs, board, players, 1070, false, false).hits.empty());
    UpdateBombs(bombs, board, players, 1600, false, false);
    Check(bombs.empty());
    for (int loser : {1, 2}) {
        IceState match;
        for (int remaining = 2; remaining >= 0; --remaining) {
            Check(ApplyIceHit(match, loser));
            Check(match.hearts[loser - 1] == remaining);
            const bool ended = FinishIceTick(match);
            Check(ended == (remaining == 0));
        }
        Check(match.result == (loser == 1 ? IceResult::Player2Win : IceResult::Player1Win));
    }

    BombFrame frame;
    frame.tick = 12; frame.sequence = 8;
    frame.bombs.push_back({123, 2, 7, 11, 2, 1, 2, 3, 4, 10});
    const auto bytes = EncodeBombState(frame);
    Check(bytes.size() == 27 && bytes[10 + 3] == 123 && bytes[14] == 2 &&
          bytes[15] == 7 && bytes[16] == 11);
    BombFrame decoded;
    Check(ReadBombState(bytes, decoded) && EncodeBombState(decoded) == bytes);
    auto bad = bytes; bad.push_back(0); Check(!ReadBombState(bad, decoded));
    bad = bytes; bad.pop_back(); Check(!ReadBombState(bad, decoded));
    bad = bytes; bad[14] = 0; Check(!ReadBombState(bad, decoded));
    bad = bytes; bad[15] = 20; Check(!ReadBombState(bad, decoded));
    bad = bytes; bad[17] = 3; Check(!ReadBombState(bad, decoded));
    bad = bytes; bad[26] = 11; Check(!ReadBombState(bad, decoded));
    bad = bytes; bad[7] = bad[6] = bad[5] = bad[4] = 0; Check(!ReadBombState(bad, decoded));
    Check(EncodeBombState(decoded) == bytes);
    frame.bombs.push_back(frame.bombs[0]); Check(EncodeBombState(frame).empty());
    bad = bytes;
    bad[9] = 2;
    bad.insert(bad.end(), bytes.begin() + 10, bytes.end());
    Check(!ReadBombState(bad, decoded)); // Duplicate ID.
    Board copy[MAP_TILES][MAP_TILES]{};
    copy[11][7] = {9, 0, 0, 2}; copy[1][1].obstacle = 6;
    frame.bombs.clear(); frame.bombs.push_back({124, 1, 7, 11, 1, 1, 1, 1, 1, 20});
    ApplyBombOccupancy(copy, frame);
    Check(copy[11][7].obstacle == 9 && copy[11][7].item == 2 && copy[1][1].obstacle == 6);
    frame.bombs.clear(); ApplyBombOccupancy(copy, frame);
    Check(copy[11][7].obstacle == 0 && copy[11][7].item == 2);
    Check(ReadBombState(EncodeBombState(frame), decoded) && decoded.bombs.empty());
    for (int flag = 0; flag < 256; ++flag) {
        InputData input;
        Check(ReadInputPayload({8, 0, 0, 0, 1, std::uint8_t(flag)}, input) == (flag < 2));
        if (flag < 2) Check(input.placeBomb == (flag == 1) && input.directionMask == kInputRight);
    }
    MovingPlayer ordered;
    Check(ordered.AcceptInput({kInputRight, 2, true}));
    Check(!ordered.AcceptInput({kInputNone, 2, true}) && !ordered.AcceptInput({kInputNone, 1, true}));
    Check(ordered.direction == kInputRight && ordered.lastSequence == 2);
    bombs.clear();
    for (auto& row : board) for (auto& cell : row) cell = {};
    auto moving = At(7, 11);
    moving.position.target = {8, 11};
    moving.position.progress = 800;
    Check(TryPlaceBomb(bombs, next, 1, moving, ability, board, 0));
    Check(bombs[0].col == 7 && board[11][7].obstacle == 9 && board[11][8].obstacle == 0);
    Check(InputPayload(kInputRight, 3, true) == (std::vector<std::uint8_t>{8, 0, 0, 0, 3, 1}));
    Check(HelloPayload() == (std::vector<std::uint8_t>{0, 1, 0, 11}));
    std::cout << "Bomb/Ice/codecs: " << checks << " checks passed.\n";
}
