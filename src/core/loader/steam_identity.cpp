#include "steam_identity.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstdint>

namespace {
using SteamUserAccessor = void *(__cdecl *)();
using SteamGetId = std::uint64_t(__cdecl *)(void *steamUser);
using SteamGetUserHandle = std::int32_t(__cdecl *)();
using SteamFindUserInterface = void *(__cdecl *)(std::int32_t steamUser, const char *version);

void SetDiagnostic(std::string *out, const char *message) {
    if (out)
        *out = message ? message : "";
}

HMODULE LoadedSteamApi() {
#if defined(_WIN64)
    if (HMODULE module = GetModuleHandleW(L"steam_api64.dll"))
        return module;
#endif
    return GetModuleHandleW(L"steam_api.dll");
}

void *ResolveSteamUser(HMODULE module) {
    // Probe newest-first; older SteamUser revisions remain compatible.
    constexpr std::array<const char *, 14> accessors = {
        "SteamAPI_SteamUser_v025", "SteamAPI_SteamUser_v024", "SteamAPI_SteamUser_v023",
        "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021", "SteamAPI_SteamUser_v020",
        "SteamAPI_SteamUser_v019", "SteamAPI_SteamUser_v018", "SteamAPI_SteamUser_v017",
        "SteamAPI_SteamUser_v016", "SteamAPI_SteamUser_v015", "SteamAPI_SteamUser_v014",
        "SteamAPI_SteamUser_v013", "SteamAPI_SteamUser_v012",
    };
    for (const char *name : accessors) {
        const auto accessor = reinterpret_cast<SteamUserAccessor>(GetProcAddress(module, name));
        if (!accessor)
            continue;
        void *steamUser = accessor();
        if (steamUser)
            return steamUser;
    }

    // Use the game's existing API context when the convenience accessor is not ready.
    const auto getUserHandle =
        reinterpret_cast<SteamGetUserHandle>(GetProcAddress(module, "SteamAPI_GetHSteamUser"));
    const auto findUserInterface = reinterpret_cast<SteamFindUserInterface>(
        GetProcAddress(module, "SteamInternal_FindOrCreateUserInterface"));
    if (!getUserHandle || !findUserInterface)
        return nullptr;

    std::int32_t userHandle = 0;
    __try {
        userHandle = getUserHandle();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (userHandle == 0)
        return nullptr;

    constexpr std::array<const char *, 14> interfaceVersions = {
        "SteamUser025", "SteamUser024", "SteamUser023", "SteamUser022", "SteamUser021", "SteamUser020",
        "SteamUser019", "SteamUser018", "SteamUser017", "SteamUser016", "SteamUser015", "SteamUser014",
        "SteamUser013", "SteamUser012",
    };
    for (const char *version : interfaceVersions) {
        void *steamUser = nullptr;
        __try {
            steamUser = findUserInterface(userHandle, version);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            steamUser = nullptr;
        }
        if (steamUser)
            return steamUser;
    }
    return nullptr;
}

bool IsPublicIndividualSteamId(std::uint64_t steamId) {
    constexpr std::uint64_t kAccountIdMask = 0xffffffffull;
    constexpr unsigned kAccountTypeShift = 52;
    constexpr unsigned kUniverseShift = 56;
    constexpr std::uint64_t kAccountTypeMask = 0xfull;
    constexpr std::uint64_t kUniverseMask = 0xffull;
    constexpr std::uint64_t kIndividualAccountType = 1;
    constexpr std::uint64_t kPublicUniverse = 1;

    const std::uint64_t accountId = steamId & kAccountIdMask;
    const std::uint64_t accountType = (steamId >> kAccountTypeShift) & kAccountTypeMask;
    const std::uint64_t universe = (steamId >> kUniverseShift) & kUniverseMask;
    return accountId != 0 && accountType == kIndividualAccountType && universe == kPublicUniverse;
}

bool TryCallGetSteamId(SteamGetId getSteamId, void *steamUser, std::uint64_t *steamIdOut) {
    if (!getSteamId || !steamUser || !steamIdOut)
        return false;
    __try {
        *steamIdOut = getSteamId(steamUser);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *steamIdOut = 0;
        return false;
    }
}
} // namespace

bool SteamIdentity_TryReadSteamId64(std::string *steamIdOut, std::string *diagnosticOut) {
    if (steamIdOut)
        steamIdOut->clear();

    HMODULE module = LoadedSteamApi();
    if (!module) {
        SetDiagnostic(diagnosticOut, "the game has not loaded steam_api64.dll or steam_api.dll");
        return false;
    }

    const auto getSteamId = reinterpret_cast<SteamGetId>(GetProcAddress(module, "SteamAPI_ISteamUser_GetSteamID"));
    if (!getSteamId) {
        SetDiagnostic(diagnosticOut, "the loaded Steam API does not export SteamAPI_ISteamUser_GetSteamID");
        return false;
    }

    void *steamUser = ResolveSteamUser(module);
    if (!steamUser) {
        SetDiagnostic(diagnosticOut, "SteamUser is unavailable; Steam may not be initialized yet");
        return false;
    }

    std::uint64_t steamId = 0;
    if (!TryCallGetSteamId(getSteamId, steamUser, &steamId)) {
        SetDiagnostic(diagnosticOut, "Steam API raised an exception while reading SteamID64");
        return false;
    }
    if (!IsPublicIndividualSteamId(steamId)) {
        SetDiagnostic(diagnosticOut, "Steam API returned an invalid or non-individual public SteamID64");
        return false;
    }

    if (steamIdOut)
        *steamIdOut = std::to_string(steamId);
    if (diagnosticOut)
        diagnosticOut->clear();
    return true;
}

bool SteamIdentity_WaitForSteamId64(unsigned timeoutMs, std::string *steamIdOut, std::string *diagnosticOut) {
    const ULONGLONG started = GetTickCount64();
    std::string lastDiagnostic;
    do {
        if (SteamIdentity_TryReadSteamId64(steamIdOut, &lastDiagnostic)) {
            if (diagnosticOut)
                diagnosticOut->clear();
            return true;
        }
        if (GetTickCount64() - started >= timeoutMs)
            break;
        Sleep(100);
    } while (true);

    if (diagnosticOut)
        *diagnosticOut = lastDiagnostic.empty() ? "Steam identity wait timed out" : lastDiagnostic;
    return false;
}
