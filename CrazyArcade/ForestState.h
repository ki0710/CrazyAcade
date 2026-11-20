#pragma once

constexpr int kForestCarryLimit = 3;
constexpr int kForestWinningTotal = 10;
constexpr int kForestBasketColumn[2] = {8, 11};
constexpr int kForestBasketRow = 19;

struct ForestState {
    int carried[2] = {0, 0};
    int deposited[2] = {0, 0};
    int winner = 0; // 0 while playing, then player ID 1 or 2.
};

// A confirmed tree hit awards the bomb owner one carried apple.
inline bool CollectForestApple(int& carried) {
    if (carried < 0 || carried >= kForestCarryLimit) return false;
    ++carried;
    return true;
}

// A confirmed player hit clears carried apples; basket totals are separate.
inline bool LoseForestApples(int& carried) {
    if (carried == 0) return false;
    carried = 0;
    return true;
}

inline bool DepositForestApples(int& carried, int& deposited, int x, int y, int boxX, int boxY) {
    const bool nextToBasket = (x == boxX && y == boxY - 1) ||
        (y == boxY && (x == boxX - 1 || x == boxX + 1));
    if (!nextToBasket || carried == 0) return false;
    deposited += carried;
    carried = 0;
    return true;
}
