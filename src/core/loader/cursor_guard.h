#pragma once

// System cursor for a native menu; game cursor calls are recorded while engaged
// and the last one is applied on Release(). Game thread only.

#include "mod_sdk.h"

namespace CursorGuard {

// Hooks user32 on first use. Shows an unclipped arrow.
bool Engage();
void Release();
bool Engaged();

// The cursor the game last asked for, engaged or not.
bool GameState(URK_CursorState *state);

} // namespace CursorGuard
