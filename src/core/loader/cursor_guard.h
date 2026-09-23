#pragma once

// Gives a native menu the system cursor without touching game state. While
// engaged, the game's ShowCursor/SetCursor/ClipCursor/SetCursorPos calls are
// recorded instead of applied; Release() applies the game's last request.
// Calls from URKit and its mods always go through. Game thread only.

#include "mod_sdk.h"

namespace CursorGuard {

// Hooks user32 on first use. Shows an unclipped arrow.
bool Engage();
void Release();
bool Engaged();

// The cursor the game last asked for, engaged or not.
bool GameState(URK_CursorState *state);

} // namespace CursorGuard
