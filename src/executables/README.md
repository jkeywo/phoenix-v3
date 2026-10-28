# Executable composition and diagnostics

These folders compose public owners into runnable roles. `native/` owns the native window/role startup; `browser/` shares one web entry point across server, client and Workshop compile-time roles. Gameplay and UI algorithms stay in their families rather than acquiring separate browser/native implementations.

`workshop-host/` is the disposable authoritative test executable/host, intentionally separate from browser authoring. `pack/` and `verify/` are content-only command-line tools. `inference-bridge/` connects an external provider protocol. `diagnostics/` owns build/profile adapters; captures are explicit evidence, not machine-speed acceptance thresholds.

Browser client and Workshop must not link authoritative execution through a convenience dependency. Native and browser adapters preserve exported callback names and request order. The migration evidence records build/runtime/trace gates separately from graphical, accessibility and public-network smoke.
