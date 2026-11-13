#pragma once

enum class IceResult {
    Playing = 0,
    Player1Win = 1,
    Player2Win = 2,
    Draw = 3
};

struct IceState {
    int hearts[2] = {3, 3};
    IceResult result = IceResult::Playing;
};

// The bomb code calls this once per bomb/player after confirming the hit.
bool ApplyIceHit(IceState& state, int playerId);
// Resolve once after all hits in this update so both players can lose together.
bool FinishIceTick(IceState& state);
