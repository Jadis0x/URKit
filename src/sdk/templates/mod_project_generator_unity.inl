// Unity SDK generation is split by the public header it produces. The first
// fragments build the canonical module text; the final fragments specialize it
// for Mono or IL2CPP and split it into standalone headers.

#include "unity/types.inl"
#include "unity/components.inl"
#include "unity/invoke.inl"
#include "unity/shortcuts.inl"
#include "unity/inspect_declarations.inl"
#include "unity/aliases.inl"
#include "unity/inspect_codegen.inl"
#include "unity/backend_codegen.inl"
#include "unity/module_split.inl"
