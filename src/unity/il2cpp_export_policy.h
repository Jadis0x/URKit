#pragma once

enum class Il2CppExportRequirement {
    Required,
    Optional,
};

struct Il2CppExportBindingDecision {
    bool bind = false;
    bool failStartup = false;
};

// Optional export failures disable only their own entry.
constexpr Il2CppExportBindingDecision
Il2CppExportPolicy_Decide(Il2CppExportRequirement requirement, bool present, bool targetValid) {
    if (!present || !targetValid)
        return {false, requirement == Il2CppExportRequirement::Required};
    return {true, false};
}

// Exact names are trusted once validated; several may share one body after ICF.
constexpr bool Il2CppExportPolicy_AcceptSharedExactTarget() {
    return true;
}
