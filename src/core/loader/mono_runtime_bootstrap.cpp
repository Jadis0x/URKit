#include "mono_runtime_bootstrap.h"
#include "hooks.h"
#include "logger.h"

#include <windows.h>

#include <algorithm>
#include <atomic>

namespace {
RuntimeState *g_state = nullptr;
std::atomic<MonoDomain *> g_monoDomain{nullptr};
HANDLE g_monoReady = nullptr;
mono_jit_init_t g_origJitInit = nullptr;
mono_jit_init_version_t g_origJitInitVersion = nullptr;

void PublishMonoDomain(MonoDomain *domain, const char *source) {
    if (!domain)
        return;

    MonoDomain *expected = nullptr;
    if (!g_monoDomain.compare_exchange_strong(expected, domain, std::memory_order_acq_rel, std::memory_order_acquire)) {
        if (expected != domain) {
            Log("[runtime][Mono][ERROR] conflicting root domains observed: "
                "published=%p candidate=%p source=%s.",
                expected, domain, source ? source : "unknown");
        }
        return;
    }

    Mono_PublishDomain(domain);
    if (g_state) {
        g_state->Transition(RuntimeReadiness::RuntimeInitialized, domain, source);
        g_state->Transition(RuntimeReadiness::DomainAvailable, domain, source);
    }
    if (g_monoReady)
        SetEvent(g_monoReady);
    Log("[runtime][Mono] domain captured from %s: %p.", source, domain);
}

MonoDomain *JitInitDetour(const char *root) {
    MonoDomain *domain = g_origJitInit(root);
    PublishMonoDomain(domain, "mono_jit_init");
    return domain;
}

MonoDomain *JitInitVersionDetour(const char *root, const char *version) {
    MonoDomain *domain = g_origJitInitVersion(root, version);
    PublishMonoDomain(domain, "mono_jit_init_version");
    return domain;
}

} // namespace

MonoRuntimeBootstrapResult MonoRuntimeBootstrap_Attach(MonoApi &mono, RuntimeState &state, int runtimeTimeoutMs) {
    g_state = &state;
    g_monoDomain.store(nullptr, std::memory_order_release);
    g_origJitInit = nullptr;
    g_origJitInitVersion = nullptr;

    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(std::max(runtimeTimeoutMs, 0));

    Log("[runtime][Mono] mono_get_root_domain initial probe begin attached=%s.",
        MonoThreadScope::CurrentThreadAttached() ? "yes" : "no");
    MonoDomain *root = mono.get_root_domain ? mono.get_root_domain() : nullptr;
    Log("[runtime][Mono] mono_get_root_domain initial probe end root=%p.", root);
    if (root)
        PublishMonoDomain(root, "mono_get_root_domain initial probe");

    bool initHooked = false;
    bool versionHooked = false;
    if (!g_monoDomain.load(std::memory_order_acquire)) {
        g_monoReady = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        if (!g_monoReady) {
            Log("[ERROR] Mono runtime readiness event creation failed: error=%lu; "
                "runtime bootstrap cannot safely coordinate init hooks.",
                GetLastError());
            return {};
        }

        Log("[runtime][Mono] installing mono_jit_init hook target=%p.", mono.jit_init);
        g_origJitInit = mono.jit_init;
        initHooked = g_origJitInit &&
                     Hook_Attach(reinterpret_cast<void **>(&g_origJitInit), reinterpret_cast<void *>(&JitInitDetour));
        Log("[runtime][Mono] mono_jit_init hook installed=%s.", initHooked ? "yes" : "no");

        Log("[runtime][Mono] installing mono_jit_init_version hook target=%p.", mono.jit_init_version);
        g_origJitInitVersion = mono.jit_init_version;
        versionHooked = g_origJitInitVersion && Hook_Attach(reinterpret_cast<void **>(&g_origJitInitVersion),
                                                            reinterpret_cast<void *>(&JitInitVersionDetour));
        Log("[runtime][Mono] mono_jit_init_version hook installed=%s.", versionHooked ? "yes" : "no");

        // Close the interval between the initial root-domain probe and hook
        // commits. If Mono initialized in that interval, no init call remains for
        // either detour to observe.
        root = mono.get_root_domain ? mono.get_root_domain() : nullptr;
        if (root && !g_monoDomain.load(std::memory_order_acquire))
            PublishMonoDomain(root, "mono_get_root_domain post-hook probe");
    } else {
        Log("[runtime][Mono] root domain already available; init hooks skipped.");
    }

    if (!g_monoDomain.load(std::memory_order_acquire)) {
        while (GetTickCount64() < deadline) {
            Log("[runtime][Mono] mono_get_root_domain begin attached=%s.",
                MonoThreadScope::CurrentThreadAttached() ? "yes" : "no");
            root = mono.get_root_domain ? mono.get_root_domain() : nullptr;
            Log("[runtime][Mono] mono_get_root_domain end root=%p.", root);
            if (root) {
                PublishMonoDomain(root, "mono_get_root_domain late discovery");
                break;
            }
            if (g_monoReady)
                WaitForSingleObject(g_monoReady, 25);
            else
                Sleep(25);
        }
    }

    bool hooksDetached = true;
    if (initHooked &&
        !Hook_Detach(reinterpret_cast<void **>(&g_origJitInit), reinterpret_cast<void *>(&JitInitDetour))) {
        Log("[ERROR] Mono runtime bootstrap could not detach mono_jit_init hook; "
            "loader initialization is unsafe to continue.");
        hooksDetached = false;
    }
    if (versionHooked && !Hook_Detach(reinterpret_cast<void **>(&g_origJitInitVersion),
                                      reinterpret_cast<void *>(&JitInitVersionDetour))) {
        Log("[ERROR] Mono runtime bootstrap could not detach "
            "mono_jit_init_version hook; loader initialization is unsafe to "
            "continue.");
        hooksDetached = false;
    }
    if (g_monoReady)
        CloseHandle(g_monoReady);
    g_monoReady = nullptr;

    if (!hooksDetached)
        return {};

    MonoDomain *publishedDomain = g_monoDomain.load(std::memory_order_acquire);
    if (!publishedDomain) {
        Log("[ERROR] Mono runtime diagnostic failure: both init hooks missed and "
            "mono_get_root_domain remained null for %d ms; mods are disabled.",
            runtimeTimeoutMs);
        return {};
    }

    MonoThread *attachedThread = nullptr;
    if (!Mono_AttachCurrentThread(mono, publishedDomain, "loader bootstrap", &attachedThread)) {
        Log("[ERROR] Failed to attach the loader thread to the Mono root domain.");
        return {publishedDomain, nullptr};
    }

    state.Transition(RuntimeReadiness::LoaderThreadAttached, publishedDomain, state.DomainSource());
    MonoDomain *current = mono.domain_get ? mono.domain_get() : nullptr;
    Log("[runtime][Mono] mono_domain_get after attach root=%p current=%p.", publishedDomain, current);
    return {publishedDomain, attachedThread};
}
