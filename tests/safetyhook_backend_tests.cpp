// Exercises the live SafetyHook engine: mid hook thunk-pool routing, the
// register copy-in/copy-out marshalling, and the inline hook trampoline. These
// break silently when SafetyHook changes its Context layout, so the assertions
// run against real patched code rather than mocks.
#include "safetyhook_backend.h"

#include <cstdarg>
#include <cstdio>

void Log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
}

namespace {

int g_hits[4] = {};
uintptr_t g_seen_rcx = 0;
uintptr_t g_seen_rdx = 0;
double g_seen_xmm2 = 0.0;
bool g_override_rcx = false;
int g_failures = 0;

void Check(bool ok, const char *what) {
    std::printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        ++g_failures;
}

void Dispatch(unsigned slot, URK_HookRegisters *registers) {
    if (slot < 4)
        ++g_hits[slot];
    g_seen_rcx = registers->rcx;
    g_seen_rdx = registers->rdx;
    g_seen_xmm2 = registers->xmm[2].f64[0];
    Check(registers->size == sizeof(URK_HookRegisters), "register context reports its own size");
    if (g_override_rcx)
        registers->rcx = 100;
}

__declspec(noinline) int TargetAdd(int a, int b) {
    volatile int x = a;
    return x + b;
}

__declspec(noinline) int TargetMul(int a, int b) {
    volatile int x = a;
    return x * b;
}

// Under the Windows x64 ABI a lands in rcx, b in rdx and c in xmm2.
__declspec(noinline) double TargetMixed(int a, int b, double c) {
    volatile double x = c;
    return x + a + b;
}

int DetourMul(int, int) {
    return 999;
}

} // namespace

int main() {
    if (!SafetyHookBackend_Available()) {
        std::printf("SafetyHook backend unavailable; nothing to verify.\n");
        return 0;
    }

    SafetyHookBackend_SetMidDispatch(&Dispatch);

    void *add_hook = SafetyHookBackend_CreateMid(reinterpret_cast<void *>(&TargetAdd), 0);
    Check(add_hook != nullptr, "create mid hook in slot 0");
    Check(TargetAdd(7, 5) == 12, "hooked function still returns its own result");
    Check(g_hits[0] == 1, "slot 0 callback fired once");
    Check(g_seen_rcx == 7, "rcx carried the first argument");
    Check(g_seen_rdx == 5, "rdx carried the second argument");

    void *mul_hook = SafetyHookBackend_CreateMid(reinterpret_cast<void *>(&TargetMul), 3);
    Check(mul_hook != nullptr, "create mid hook in slot 3");
    TargetMul(6, 4);
    Check(g_hits[3] == 1, "slot 3 callback fired once");
    Check(g_hits[0] == 1, "slot 3 did not dispatch into slot 0");
    Check(g_seen_rcx == 6, "slot 3 observed its own registers");

    void *mixed_hook = SafetyHookBackend_CreateMid(reinterpret_cast<void *>(&TargetMixed), 1);
    Check(mixed_hook != nullptr, "create mid hook in slot 1");
    TargetMixed(1, 2, 2.5);
    Check(g_seen_xmm2 == 2.5, "xmm2 carried the double argument");

    g_override_rcx = true;
    Check(TargetAdd(7, 5) == 105, "register writes reach the resumed function");
    g_override_rcx = false;

    Check(SafetyHookBackend_SetMidEnabled(add_hook, false), "disable a mid hook");
    const int hits_while_disabled = g_hits[0];
    TargetAdd(1, 1);
    Check(g_hits[0] == hits_while_disabled, "disabled mid hook does not fire");
    Check(SafetyHookBackend_SetMidEnabled(add_hook, true), "re-enable a mid hook");
    TargetAdd(1, 1);
    Check(g_hits[0] == hits_while_disabled + 1, "re-enabled mid hook fires again");

    Check(SafetyHookBackend_DestroyMid(add_hook), "destroy a mid hook");
    const int hits_after_destroy = g_hits[0];
    Check(TargetAdd(2, 3) == 5, "function restored after mid hook destroy");
    Check(g_hits[0] == hits_after_destroy, "destroyed mid hook does not fire");
    SafetyHookBackend_DestroyMid(mul_hook);
    SafetyHookBackend_DestroyMid(mixed_hook);

    void *trampoline = nullptr;
    void *inline_hook = SafetyHookBackend_CreateInline(reinterpret_cast<void *>(&TargetMul),
                                                       reinterpret_cast<void *>(&DetourMul), &trampoline);
    Check(inline_hook != nullptr && trampoline != nullptr, "create an inline hook");
    Check(TargetMul(6, 4) == 999, "inline detour intercepts the call");
    Check(reinterpret_cast<int (*)(int, int)>(trampoline)(6, 4) == 24, "trampoline reaches the original code");
    Check(SafetyHookBackend_DestroyInline(inline_hook), "destroy an inline hook");
    Check(TargetMul(6, 4) == 24, "function restored after inline hook destroy");

    std::printf("\n%s (%d failures)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
