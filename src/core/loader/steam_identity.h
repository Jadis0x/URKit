#pragma once

#include <string>

// Reads the current public individual SteamID64 from the Steam API module that
// the game has already loaded. This deliberately does not initialize or shut
// down Steamworks on the loader's behalf.
bool SteamIdentity_TryReadSteamId64(std::string *steamIdOut, std::string *diagnosticOut = nullptr);

// Waits for games that initialize Steamworks after the Unity runtime becomes
// ready. Internal callers should avoid blocking the Unity main thread.
bool SteamIdentity_WaitForSteamId64(unsigned timeoutMs, std::string *steamIdOut,
                                    std::string *diagnosticOut = nullptr);
