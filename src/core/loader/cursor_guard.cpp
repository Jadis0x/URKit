#include "cursor_guard.h"

#include "logger.h"
#include "mod_lifecycle.h"
#include "safetyhook_backend.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <mutex>

namespace {

using ShowCursorFn = int(WINAPI *)(BOOL);
using SetCursorFn = HCURSOR(WINAPI *)(HCURSOR);
using ClipCursorFn = BOOL(WINAPI *)(const RECT *);
using SetCursorPosFn = BOOL(WINAPI *)(int, int);

// Trampolines once hooked; our own calls go through these, never the detours.
std::atomic<ShowCursorFn> g_showCursor{nullptr};
std::atomic<SetCursorFn> g_setCursor{nullptr};
std::atomic<ClipCursorFn> g_clipCursor{nullptr};
std::atomic<SetCursorPosFn> g_setCursorPos{nullptr};

std::once_flag g_installOnce;
bool g_installed = false;

std::atomic<bool> g_engaged{false};
std::mutex g_stateMutex;
// The game's own requests while engaged.
int g_gameCount = 0;
HCURSOR g_gameCursor = nullptr;
bool g_gameClipped = false;
RECT g_gameClip{};

HMODULE ModuleOf(const void *address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(address), &module);
    return module;
}

// Everything but URKit and its mods: the engine, and user32 acting for it.
bool FromGame(const void *returnAddress) {
    static const HMODULE self = ModuleOf(reinterpret_cast<const void *>(&CursorGuard::Engage));
    const HMODULE caller = ModuleOf(returnAddress);
    return caller != self && !ModLifecycle_IsModModule(caller);
}

bool CoversVirtualScreen(const RECT &rect) {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return rect.left <= left && rect.top <= top && rect.right >= left + GetSystemMetrics(SM_CXVIRTUALSCREEN) &&
           rect.bottom >= top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

// Windows exposes the display count only through ShowCursor's return value.
int CurrentCount(ShowCursorFn show) {
    const int raised = show(TRUE);
    show(FALSE);
    return raised - 1;
}

int WINAPI ShowCursorDetour(BOOL show) {
    if (g_engaged.load(std::memory_order_acquire) && FromGame(_ReturnAddress())) {
        std::lock_guard lock(g_stateMutex);
        g_gameCount += show ? 1 : -1;
        return g_gameCount;
    }
    return g_showCursor.load(std::memory_order_acquire)(show);
}

HCURSOR WINAPI SetCursorDetour(HCURSOR cursor) {
    if (g_engaged.load(std::memory_order_acquire) && FromGame(_ReturnAddress())) {
        std::lock_guard lock(g_stateMutex);
        const HCURSOR previous = g_gameCursor;
        g_gameCursor = cursor;
        return previous;
    }
    return g_setCursor.load(std::memory_order_acquire)(cursor);
}

BOOL WINAPI ClipCursorDetour(const RECT *rect) {
    if (g_engaged.load(std::memory_order_acquire) && FromGame(_ReturnAddress())) {
        std::lock_guard lock(g_stateMutex);
        g_gameClipped = rect != nullptr;
        g_gameClip = rect ? *rect : RECT{};
        return TRUE;
    }
    return g_clipCursor.load(std::memory_order_acquire)(rect);
}

// A position is not state: a game recentring the cursor is dropped, not replayed.
BOOL WINAPI SetCursorPosDetour(int x, int y) {
    if (g_engaged.load(std::memory_order_acquire) && FromGame(_ReturnAddress()))
        return TRUE;
    return g_setCursorPos.load(std::memory_order_acquire)(x, y);
}

template <typename Fn> bool Hook(HMODULE user32, const char *name, Fn detour, std::atomic<Fn> &original,
                                 void *&handle) {
    void *target = reinterpret_cast<void *>(GetProcAddress(user32, name));
    void *trampoline = nullptr;
    handle = target ? SafetyHookBackend_CreateInline(target, reinterpret_cast<void *>(detour), &trampoline) : nullptr;
    if (!handle || !trampoline) {
        if (handle)
            SafetyHookBackend_DestroyInline(handle);
        handle = nullptr;
        Log("[cursor][ERROR] could not hook user32!%s.", name);
        return false;
    }
    original.store(reinterpret_cast<Fn>(trampoline), std::memory_order_release);
    return true;
}

// All or none: a partial set would leave some game calls applied.
void Install() {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32 || !SafetyHookBackend_Available()) {
        Log("[cursor][ERROR] user32 or the hook engine is unavailable; menu cursor stays off.");
        return;
    }
    void *handles[4]{};
    const bool ok = Hook(user32, "ShowCursor", &ShowCursorDetour, g_showCursor, handles[0]) &&
                    Hook(user32, "SetCursor", &SetCursorDetour, g_setCursor, handles[1]) &&
                    Hook(user32, "ClipCursor", &ClipCursorDetour, g_clipCursor, handles[2]) &&
                    Hook(user32, "SetCursorPos", &SetCursorPosDetour, g_setCursorPos, handles[3]);
    if (!ok) {
        for (void *handle : handles) {
            if (handle)
                SafetyHookBackend_DestroyInline(handle);
        }
        return;
    }
    g_installed = true;
}

} // namespace

namespace CursorGuard {

bool Engage() {
    if (g_engaged.load(std::memory_order_acquire))
        return true;
    std::call_once(g_installOnce, &Install);
    if (!g_installed)
        return false;

    const ShowCursorFn show = g_showCursor.load(std::memory_order_acquire);
    {
        std::lock_guard lock(g_stateMutex);
        // Not engaged yet, so Windows holds exactly what the game asked for.
        g_gameCount = CurrentCount(show);
        g_gameCursor = GetCursor();
        RECT clip{};
        g_gameClipped = GetClipCursor(&clip) && !CoversVirtualScreen(clip);
        g_gameClip = clip;
        g_engaged.store(true, std::memory_order_release);
    }

    while (show(TRUE) < 0) {
    }
    g_setCursor.load(std::memory_order_acquire)(LoadCursor(nullptr, IDC_ARROW));
    g_clipCursor.load(std::memory_order_acquire)(nullptr);
    return true;
}

void Release() {
    if (!g_engaged.exchange(false, std::memory_order_acq_rel))
        return;

    const ShowCursorFn show = g_showCursor.load(std::memory_order_acquire);
    std::lock_guard lock(g_stateMutex);
    int count = CurrentCount(show);
    while (count < g_gameCount)
        count = show(TRUE);
    while (count > g_gameCount)
        count = show(FALSE);
    g_setCursor.load(std::memory_order_acquire)(g_gameCursor);
    g_clipCursor.load(std::memory_order_acquire)(g_gameClipped ? &g_gameClip : nullptr);
}

bool Engaged() { return g_engaged.load(std::memory_order_acquire); }

bool GameState(URK_CursorState *state) {
    if (!state)
        return false;
    if (g_engaged.load(std::memory_order_acquire)) {
        std::lock_guard lock(g_stateMutex);
        // Hidden either way: a negative count, or no cursor image (UE's EMouseCursor::None).
        state->visible = g_gameCount >= 0 && g_gameCursor != nullptr ? 1 : 0;
        state->lockState = g_gameClipped ? URK_CURSOR_LOCK_CONFINED : URK_CURSOR_LOCK_NONE;
        return true;
    }
    CURSORINFO info{sizeof(info)};
    RECT clip{};
    if (!GetCursorInfo(&info) || !GetClipCursor(&clip))
        return false;
    state->visible = (info.flags & CURSOR_SHOWING) != 0 ? 1 : 0;
    state->lockState = CoversVirtualScreen(clip) ? URK_CURSOR_LOCK_NONE : URK_CURSOR_LOCK_CONFINED;
    return true;
}

} // namespace CursorGuard
