#pragma once

#include <cstdint>

constexpr int kCityStartSeconds = 30;

// Keep the local train's route, speed and nominal 12 ms step.
constexpr int kCityTrainStartX = -690;
constexpr int kCityTrainEndX = 1125;
constexpr int kCityTrainStepPixels = 5;
constexpr int kCityTrainTravelSteps = (kCityTrainEndX - kCityTrainStartX) / kCityTrainStepPixels + 1;
constexpr int kCityTrainStepMs = 12;
constexpr int kCityTrainWaitMs = 2400;

enum class CityResult : std::uint8_t { Playing, Player1Win, Player2Win, Draw };

struct CityState {
    int remaining[2] = {kCityStartSeconds, kCityStartSeconds};
    CityResult result = CityResult::Playing;
    int trainStep = 0; // 0 is hidden; active positions are numbered from 1.
};

inline bool AdvanceCityClock(CityState& state, std::uint64_t seconds) {
    if (!seconds || state.result != CityResult::Playing) return false;
    for (int& remaining : state.remaining)
        remaining = seconds >= static_cast<std::uint64_t>(remaining) ? 0 : remaining - static_cast<int>(seconds);
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
    constexpr int cycleMs = kCityTrainWaitMs + kCityTrainTravelSteps * kCityTrainStepMs;
    const int phaseMs = static_cast<int>(elapsedMs % cycleMs);
    const int step = phaseMs < kCityTrainWaitMs ? 0 :
        (phaseMs - kCityTrainWaitMs) / kCityTrainStepMs + 1;
    if (state.trainStep == step) return false;
    state.trainStep = step;
    return true;
}
