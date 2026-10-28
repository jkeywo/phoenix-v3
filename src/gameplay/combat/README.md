# Combat state and reusable outcomes

Read `state.cppm` (`phoenix.gameplay.combat.state`, target `phoenix_gameplay_combat_state`) for native structure, shield, projectile and combat continuation records. `serialization.cpp` gives those records canonical JSON ownership. `shields.cppm`, `faults.cppm` and `outcomes.cppm` expose the separately compiled shield-grant, structural eligibility and candidate-outcome mechanisms.

These targets depend on physical state and combat/effect/process contracts. They do not own a World tick or renderer. Higher services in weapons, directed, damage, ordnance, repairs and combat-continuation compose the mechanisms through their own interfaces.

Current native phase wiring is in the ordinary implementation units `src/world/bindings/gameplay_combat_*.cpp` and `gameplay_directed.cpp`. They replace the former included class-body headers. Some integration policy still remains there; importing the public combat mechanisms does not grant or complete the entire World adapter.

Combat, shield, directed, personnel and swept-ordnance journeys plus exact saves are the behavioral checks. Preserve grant identity, layer order, damage/fault semantics and serialization defaults. The [verification record](../../../docs/modularisation-evidence.md) records the completed native and ordinary/threaded WASM project gates and remaining environmental checks.
