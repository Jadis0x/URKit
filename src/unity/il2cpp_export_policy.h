#pragma once

enum class Il2CppExportRequirement {
    Required,
    Optional,
};

struct Il2CppExportBindingDecision {
    bool bind = false;
    bool failStartup = false;
};

// Presence and target validation are evaluated per export. Optional failures
// degrade only the corresponding API entry; they must never poison the core
// runtime binding result.
constexpr Il2CppExportBindingDecision
Il2CppExportPolicy_Decide(Il2CppExportRequirement requirement, bool present, bool targetValid) {
    if (!present || !targetValid)
        return {false, requirement == Il2CppExportRequirement::Required};
    return {true, false};
}

// Exact PE export names remain authoritative when their targets pass module,
// executable-page, and GetProcAddress cross-validation. Multiple exact names
// may legitimately share an implementation after identical-code folding or
// when Unity emits common no-op compatibility stubs.
constexpr bool Il2CppExportPolicy_AcceptSharedExactTarget() {
    return true;
}
