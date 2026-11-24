#pragma once

#include <cstdint>

constexpr int kCityStartSeconds = 30;

enum class CityResult : std::uint8_t { Playing, Player1Win, Player2Win, Draw };

struct CityState {
    int remaining[2] = {kCityStartSeconds, kCityStartSeconds};
    CityResult result = CityResult::Playing;
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
