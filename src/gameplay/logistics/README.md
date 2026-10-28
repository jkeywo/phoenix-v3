# Native logistics and cargo accounting

Read `state.cppm` (`phoenix.gameplay.logistics.state`) first for physical stores, inventory, requests, batches, cargo jobs and servicing attribution. `state.cpp` owns their exact continuation encoding. Effects may import this state-only target without acquiring World or domain orchestration.

`logistics.cppm` (`phoenix.gameplay.logistics`) and `native.cpp` own cargo commitment preparation, dispatch staging, pending/available accounting physical batch transit, resource selection with stale target/request invalidation, and the atomic receiving reservation transfer when a batch returns to its source. The only collaborators expose stock identities, physical route/edge state and docking gateways. StockTransaction still deduplicates actual stores and commits atomically.

Transit advances one batch at a time and returns control for its receiving/service commit before another batch advances. This preserves phase order when completion effects change later availability. A physically arrived batch is not automatically a delivered receipt. Cargo attribution is updated only through the existing receiving path.

`src/world/travel-bindings/supply_admission.cpp`, `gameplay_logistics_02.cpp` and `gameplay_cargo.cpp` are ordinary World composition adapters for Console authority, source selection, scenario delivery orchestration and finite native-work commits. Their remaining contextual extraction is separate from the independently compiled accounting/transit implementation.

Relevant checks: stock accounting, logistics, cargo, docking, servicing/finite work and save/restore/native-WASM continuation. Production tuning is not a golden test surface.
