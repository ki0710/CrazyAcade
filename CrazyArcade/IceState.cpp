#include "IceState.h"

bool ApplyIceHit(IceState& state, int playerId) {
    if (state.result != IceResult::Playing || playerId < 1 || playerId > 2) return false;
    int& hearts = state.hearts[playerId - 1];
    if (hearts == 0) return false;
    --hearts;
    return true;
}

bool FinishIceTick(IceState& state) {
    if (state.result != IceResult::Playing ||
        (state.hearts[0] > 0 && state.hearts[1] > 0)) return false;

    if (state.hearts[0] == 0 && state.hearts[1] == 0)
        state.result = IceResult::Draw;
    else if (state.hearts[0] == 0)
        state.result = IceResult::Player2Win;
    else
        state.result = IceResult::Player1Win;
    return true;
}
