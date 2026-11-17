#pragma once
#include <limits>

struct ForestState {
    int carried[2] = {0, 0};
};

// A confirmed tree hit awards the bomb owner one carried apple.
inline bool CollectForestApple(int& carried) {
    if (carried < 0 || carried == (std::numeric_limits<int>::max)()) return false;
    ++carried;
    return true;
}

// A confirmed player hit clears carried apples; basket totals are separate.
inline bool LoseForestApples(int& carried) {
    if (carried == 0) return false;
    carried = 0;
    return true;
}
