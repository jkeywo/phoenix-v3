# Property effects

`phoenix.effects.properties` owns modifier applications, native/custom aggregate records and checked property arithmetic. Pure preparation functions receive immutable definitions and an explicit catalogue or precompiled values.

`phoenix.effects.properties.runtime` owns the authoritative ECS modifier queries and membership, target keys, aggregate caches, snapshot reconciliation, expiry, target pruning and modifier persistence. `PropertyApplicationStore` holds the real queries; application state remains on native ECS components. Native adapters provide actor-specific logical-target validity, restore compatibility and grant availability. Three mutation notifications bracket custom-property capture and event publication in the existing nested order.

Read `properties.cppm` for records and preparation, then `runtime.cppm` and `runtime.cpp` for application ownership. Authoritative ECS handles never enter player projections. The runtime and its native bindings passed the final native and ordinary/threaded WASM project gates; see the [verification record](../../../docs/modularisation-evidence.md). Existing property, rules and store-effect system cases exercise contribution lifetime and transaction behaviour.

Low-level restore installs owner records incrementally and is intended for a fresh candidate. Public World restore supplies that candidate, suppresses mutation events during hydration and swaps it into service only after all validation succeeds. In-place callers would need their own staging.
