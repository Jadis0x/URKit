# URKit - Mono SDK

This folder is generated for Mono native header-only mods. Generated SDK files use `.h`.

Mono and IL2CPP generated projects use runtime API helpers. No offline metadata or dump-generated headers are emitted. Mono helpers resolve classes, methods, fields, properties, and objects through the public runtime API at mod runtime. Use `URK::mono::helpers::require_*` helpers to log diagnostics for missing or changed members.

Mono and IL2CPP generated projects use runtime API helpers. No offline metadata or dump-generated modules are emitted.
