# First combat stage verification

20 October 2026, Windows native Release and Emscripten/WebGL, branch `codex/combat-cannons`.

## Automated evidence

- All 23 native CTest cases pass, including `system.combat`; native build-check and production pack verification pass.
- The complete WASM gate passes, including production launch preflight, content, Workshop, session and simulation cases. Native/WASM continuation hashes match for the existing simulation fixture and for a synthetic combat fixture saved with a projectile in flight, magazine refill and a travelling repair team. The combat trace excludes only the synthetic ZIP archive identity (which contains creation timestamps); restoration still validates that identity within each run.
- Combat fixtures exercise persistent targeting, shared-turret retargeting and reassertion, cancellation, authority, finite ammunition, energy-gated shots/refills, penetration and conserved damage, cover, traversal limits, compartment recovery, routes and diminishing repairs, collisions, destruction, scoped projections and restoration. Fixtures do not freeze shipped balance.
- All eight network cases pass on the final complete rerun. An earlier run passed seven of eight cases while the live-session GM-approval case timed out during concurrent builds; that case also passed a focused rerun before the final complete run.

Local logs are `build/combat-final-native-build.log`, `build/combat-final-native-tests.log`, `build/combat-final-build-check.log`, `build/combat-final-web-gate.log`, `build/combat-final-network-gate.log`, `build/combat-network-gate.log` and `build/combat-network-focused.log`. Generated output is ignored by Git.

## Manually invoked graphical evidence

`phoenix_combat_smoke` is an explicitly invoked native target, excluded from the normal build and CTest. Build with CMake's `--target phoenix_combat_smoke --config Release`, then run its executable with the absolute `build/native` directory and optional `support` or `destruction` mode.

The normal journey loads the authored duel through production paths, opens Helm and Tactical, selects the pirate and submits admitted weapon assignments. Captures show the scene, projectiles, asteroids and independently articulated pirate turrets. Support selects a compartment through the rendered map and assigns a team through its exposed action; observation confirms travel. The destruction journey uses an explicit synthetic child override to arrange a reproducible near-destroyed target, then actual weapon fire triggers growth, ship removal, fading, Victory and continued session operation.

Inspected captures in `build/native`: `combat-helm.png`, `combat-tactical.png`, `combat-scene.png`, `combat-support.png`, `combat-explosion-growing.png` and `combat-explosion-fading.png`. The final growth capture confirms the billboard depth correction removes the earlier ship/effect interference. Smoke output contains the existing Windows navigation protocol warning; the corrected combat controls introduce no RmlUi property or shader errors.

A separately invoked in-app browser journey hosted Asteroid duel, selected Armed Courier, readied and started the session. WebGL showed the pirate, articulated turrets and asteroid region while simulation advanced. External public signalling failed in this environment; this verifies local browser hosting/rendering, not a remote browser crew connection. Native network evidence above covers transport independently.

## Acceptance limits

These runs do not establish balance, actual screen-reader or physical-device operation, audible quality, six-display performance, or a remote browser crew journey. Later combat features listed in the implementation contract remain deferred.

## Original-plan review corrections, 21 October 2026

An independent agent compared the implementation with the original combat requirements and inspected production paths and authored content. Four confirmed mechanism gaps were corrected: turret aiming now compensates for the selected gun's local bearing and lateral muzzle offset; ended duels clear readiness and zero shot/reload resource requests; duplicate undirected repair edges are rejected; and projectile sweeps stream physical asteroid cover beyond ship windows. The agent reviewed the corrections and found no further blocker.

A new synthetic offset-gun case reproduced the aiming failure before correction. Additional integration cases exercise continued firing after traversal settles, zero combat energy costs after a duel outcome, reversed duplicate-edge rejection, and an in-flight shot encountering a previously unstreamed remote field. These use the real content, command, continuation and physics paths. All 23 native CTest cases, native build-check, the complete WASM gate and native/WASM combat continuation comparison pass after these corrections. Formatting and patch-whitespace checks pass. Local logs use the `build/combat-review-` prefix.
