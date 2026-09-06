#include "safetyhook_backend.h"

#if URK_WITH_SAFETYHOOK

#include "logger.h"

#include <array>
#include <cstring>
#include <utility>

#include <safetyhook.hpp>

namespace {

SafetyHookBackend_MidDispatchFn g_mid_dispatch = nullptr;

#if defined(_WIN64)
static_assert(offsetof(safetyhook::Context, xmm15) - offsetof(safetyhook::Context, xmm0) ==
                  15 * sizeof(safetyhook::Context::xmm0),
              "SafetyHook xmm registers are expected to be contiguous.");
static_assert(sizeof(safetyhook::Context::xmm0) == sizeof(URK_HookXmmRegister),
              "SafetyHook xmm register width must match the SDK register context.");

void CopyContextIn(const safetyhook::Context &ctx, URK_HookRegisters *registers) {
    std::memset(registers, 0, sizeof(*registers));
    registers->size = sizeof(*registers);
    std::memcpy(registers->xmm, &ctx.xmm0, sizeof(registers->xmm));
    registers->rflags = ctx.rflags;
    registers->r15 = ctx.r15;
    registers->r14 = ctx.r14;
    registers->r13 = ctx.r13;
    registers->r12 = ctx.r12;
    registers->r11 = ctx.r11;
    registers->r10 = ctx.r10;
    registers->r9 = ctx.r9;
    registers->r8 = ctx.r8;
    registers->rdi = ctx.rdi;
    registers->rsi = ctx.rsi;
    registers->rdx = ctx.rdx;
    registers->rcx = ctx.rcx;
    registers->rbx = ctx.rbx;
    registers->rax = ctx.rax;
    registers->rbp = ctx.rbp;
    registers->rsp = ctx.rsp;
    registers->trampoline_rsp = ctx.trampoline_rsp;
    registers->rip = ctx.rip;
}

// rsp is deliberately not written back: SafetyHook ignores it and the mod is
// expected to redirect the stack through trampoline_rsp instead.
void CopyContextOut(const URK_HookRegisters &registers, safetyhook::Context *ctx) {
    std::memcpy(&ctx->xmm0, registers.xmm, sizeof(registers.xmm));
    ctx->rflags = registers.rflags;
    ctx->r15 = registers.r15;
    ctx->r14 = registers.r14;
    ctx->r13 = registers.r13;
    ctx->r12 = registers.r12;
    ctx->r11 = registers.r11;
    ctx->r10 = registers.r10;
    ctx->r9 = registers.r9;
    ctx->r8 = registers.r8;
    ctx->rdi = registers.rdi;
    ctx->rsi = registers.rsi;
    ctx->rdx = registers.rdx;
    ctx->rcx = registers.rcx;
    ctx->rbx = registers.rbx;
    ctx->rax = registers.rax;
    ctx->rbp = registers.rbp;
    ctx->trampoline_rsp = registers.trampoline_rsp;
    ctx->rip = registers.rip;
}

void DispatchSlot(unsigned slot, safetyhook::Context &ctx) {
    SafetyHookBackend_MidDispatchFn dispatch = g_mid_dispatch;
    if (!dispatch)
        return;

    URK_HookRegisters registers{};
    CopyContextIn(ctx, &registers);
    dispatch(slot, &registers);
    CopyContextOut(registers, &ctx);
}

template <unsigned Slot> void MidThunk(safetyhook::Context &ctx) {
    DispatchSlot(Slot, ctx);
}

template <unsigned... Slots>
constexpr std::array<safetyhook::MidHookFn, sizeof...(Slots)> MakeThunks(std::integer_sequence<unsigned, Slots...>) {
    return {&MidThunk<Slots>...};
}

constexpr auto g_mid_thunks = MakeThunks(std::make_integer_sequence<unsigned, SafetyHookBackend_MidSlotCount>{});
#endif

} // namespace

bool SafetyHookBackend_Available() {
#if defined(_WIN64)
    return true;
#else
    return false;
#endif
}

void *SafetyHookBackend_CreateInline(void *target, void *detour, void **trampoline) {
    if (!target || !detour || !trampoline || !SafetyHookBackend_Available())
        return nullptr;

    auto created = safetyhook::InlineHook::create(target, detour);
    if (!created) {
        Log("[ERROR] SafetyHook inline hook creation failed: target=%p detour=%p error=%d.", target, detour,
            static_cast<int>(created.error().type));
        return nullptr;
    }

    auto *hook = new (std::nothrow) safetyhook::InlineHook(std::move(*created));
    if (!hook)
        return nullptr;

    // original<void*>() avoids original<uintptr_t>(), which some MSVC toolsets
    // reject: the library's template body does reinterpret_cast<T>(uintptr_t),
    // and T=uintptr_t makes that an identity reinterpret_cast that newer MSVC
    // flags as ill-formed even though it's standard-permitted.
    *trampoline = hook->original<void *>();
    return hook;
}

bool SafetyHookBackend_DestroyInline(void *handle) {
    if (!handle)
        return false;
    delete static_cast<safetyhook::InlineHook *>(handle);
    return true;
}

void SafetyHookBackend_SetMidDispatch(SafetyHookBackend_MidDispatchFn dispatch) {
    g_mid_dispatch = dispatch;
}

void *SafetyHookBackend_CreateMid(void *target, unsigned slot) {
#if defined(_WIN64)
    if (!target || slot >= SafetyHookBackend_MidSlotCount)
        return nullptr;

    auto created = safetyhook::MidHook::create(target, g_mid_thunks[slot]);
    if (!created) {
        Log("[ERROR] SafetyHook mid hook creation failed: target=%p error=%d.", target,
            static_cast<int>(created.error().type));
        return nullptr;
    }

    auto *hook = new (std::nothrow) safetyhook::MidHook(std::move(*created));
    return hook;
#else
    (void)target;
    (void)slot;
    return nullptr;
#endif
}

bool SafetyHookBackend_DestroyMid(void *handle) {
    if (!handle)
        return false;
    delete static_cast<safetyhook::MidHook *>(handle);
    return true;
}

bool SafetyHookBackend_SetMidEnabled(void *handle, bool enabled) {
    if (!handle)
        return false;
    auto *hook = static_cast<safetyhook::MidHook *>(handle);
    return enabled ? hook->enable().has_value() : hook->disable().has_value();
}

#else // URK_WITH_SAFETYHOOK

bool SafetyHookBackend_Available() {
    return false;
}

void *SafetyHookBackend_CreateInline(void *, void *, void **) {
    return nullptr;
}

bool SafetyHookBackend_DestroyInline(void *) {
    return false;
}

void SafetyHookBackend_SetMidDispatch(SafetyHookBackend_MidDispatchFn) {}

void *SafetyHookBackend_CreateMid(void *, unsigned) {
    return nullptr;
}

bool SafetyHookBackend_DestroyMid(void *) {
    return false;
}

bool SafetyHookBackend_SetMidEnabled(void *, bool) {
    return false;
}

#endif // URK_WITH_SAFETYHOOK
