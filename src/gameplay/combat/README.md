# Combat state and reusable outcomes

Read `state.cppm` (`phoenix.gameplay.combat.state`, target `phoenix_gameplay_combat_state`) for native structure, shield, projectile and combat continuation records. `serialization.cpp` gives those records canonical JSON ownership. `shields.cppm`, `faults.cppm` and `outcomes.cppm` expose the separately compiled shield-grant, structural eligibility and candidate-outcome mechanisms.

These targets depend on physical state and combat/effect/process contracts. They do not own a World tick or renderer. Higher services in weapons, directed, damage, ordnance, repairs and combat-continuation compose the mechanisms through their own interfaces.

The remaining `combat_*.hpp` and `directed.hpp` files are World integration fragments still being reduced; reading them is necessary for current native phase wiring, but importing the public mechanisms does not grant the entire World adapter.

Combat, shield, directed, personnel and swept-ordnance journeys plus exact saves are the behavioral checks. Preserve grant identity, layer order, damage/fault semantics and serialization defaults. The central evidence distinguishes verified owner checkpoints from pending final World/platform gates.
