#pragma once

#include <cstdint>

constexpr int kCityStartSeconds = 30;

// Keep the local train's route, speed and nominal 12 ms step.
constexpr int kCityTrainStartX = -690;
constexpr int kCityTrainTop = 320;
constexpr int kCityTrainWidth = 1029;
constexpr int kCityTrainHeight = 101;
constexpr int kCityTrainEndX = 1125;
constexpr int kCityTrainStepPixels = 5;
constexpr int kCityTrainTravelSteps = (kCityTrainEndX - kCityTrainStartX) / kCityTrainStepPixels + 1;
constexpr int kCityTrainStepMs = 12;
constexpr int kCityTrainWaitMs = 2400;
constexpr int kCityTrainCycleMs = kCityTrainWaitMs + kCityTrainTravelSteps * kCityTrainStepMs;
constexpr int kCityTrainHitSeconds = 5;
constexpr int kCityBombHitSeconds = 2;
constexpr int kCityClockBonusSeconds = 5;
constexpr int kCityMaxSeconds = 65535; // Largest time the wire format can carry.
constexpr int kCityClockItem = 4;
constexpr int kCityDropCount = 6;
constexpr int kCityDropTrainX = 200;
constexpr int kCityDropTrainStep = (kCityDropTrainX - kCityTrainStartX) / kCityTrainStepPixels + 1;
constexpr int kCityDropStartX = kCityTrainWidth - 250;
constexpr int kCityDropStartY = kCityTrainTop + kCityTrainHeight - 40;
constexpr int kCityDropStepPixels = 10;

inline int CityTrainLeft(int step) {
    return kCityTrainStartX + (step - 1) * kCityTrainStepPixels;
}

inline bool CityTrainHits(int step, int x, int y) {
    if (step == 0) return false;
    const int left = CityTrainLeft(step);
    return x >= left && x <= left + kCityTrainWidth &&
           y >= kCityTrainTop && y <= kCityTrainTop + kCityTrainHeight;
}

enum class CityResult : std::uint8_t { Playing, Player1Win, Player2Win, Draw };

struct CityFallingItem {
    int x = 0, y = 0, item = 0; // Logical screen position; item 0 is inactive.
};

struct CityState {
    int remaining[2] = {kCityStartSeconds, kCityStartSeconds};
    CityResult result = CityResult::Playing;
    int trainStep = 0; // 0 is hidden; active positions are numbered from 1.
    CityFallingItem fallingItems[kCityDropCount];
};

inline bool AdvanceCityClock(CityState& state, std::uint64_t seconds) {
    if (!seconds || state.result != CityResult::Playing) return false;
    for (int& remaining : state.remaining)
        remaining = seconds >= static_cast<std::uint64_t>(remaining) ? 0 : remaining - static_cast<int>(seconds);
    return true;
}

inline bool ReduceCityTime(CityState& state, int index, int seconds) {
    int& remaining = state.remaining[index];
    if (remaining == 0) return false;
    remaining = remaining > seconds ? remaining - seconds : 0;
    return true;
}

inline bool FinishCityTick(CityState& state) {
    if (state.result != CityResult::Playing ||
        (state.remaining[0] > 0 && state.remaining[1] > 0)) return false;
    if (state.remaining[0] == 0 && state.remaining[1] == 0) state.result = CityResult::Draw;
    else if (state.remaining[0] == 0) state.result = CityResult::Player2Win;
    else state.result = CityResult::Player1Win;
    return true;
}

inline bool UpdateCityTrain(CityState& state, std::uint64_t elapsedMs) {
    if (state.result != CityResult::Playing) return false;
    const int phaseMs = static_cast<int>(elapsedMs % kCityTrainCycleMs);
    const int step = phaseMs < kCityTrainWaitMs ? 0 :
        (phaseMs - kCityTrainWaitMs) / kCityTrainStepMs + 1;
    if (state.trainStep == step) return false;
    state.trainStep = step;
    return true;
}
