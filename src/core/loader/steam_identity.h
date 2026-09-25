#pragma once

#include <string>

// SteamID64 from the game's loaded Steam API; never initializes Steamworks.
bool SteamIdentity_TryReadSteamId64(std::string *steamIdOut, std::string *diagnosticOut = nullptr);

// Waits for late Steamworks init; don't call from the Unity main thread.
bool SteamIdentity_WaitForSteamId64(unsigned timeoutMs, std::string *steamIdOut,
                                    std::string *diagnosticOut = nullptr);
