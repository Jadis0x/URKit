// sdk/unreal/unreal_runtime.h: a thin C++ face over URK_UnrealApi, in the headers under runtime/.

#include "runtime/core.inl"
#include "runtime/values.inl"
#include "runtime/calls.inl"
#include "runtime/world.inl"
#include "runtime/typed.inl"

std::string UnrealRuntimeModule() {
    return R"URKUE(#pragma once

// Everything in runtime/; include a single one of them to take less.
#include "runtime/typed.h"
)URKUE";
}

// Published beside unreal_runtime.h, in include order.
struct UnrealRuntimePiece {
    const char *file;
    std::string (*text)();
};

inline const UnrealRuntimePiece kUnrealRuntimePieces[] = {
    {"runtime/core.h", &UnrealRuntimeCore},
    {"runtime/values.h", &UnrealRuntimeValues},
    {"runtime/calls.h", &UnrealRuntimeCalls},
    {"runtime/world.h", &UnrealRuntimeWorld},
    {"runtime/typed.h", &UnrealRuntimeTyped},
};
