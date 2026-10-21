# Combat MVP verification

21 October 2026, Windows native Release and Emscripten/WebGL, branch `codex/combat-mvp`, based on the completed `codex/combat-cannons` commit `d2e2f63`.

## Automated evidence

- All 23 native CTest cases pass, including combat and the Console choice regression; native build-check passes.
- The complete WASM gate passes, including production content preflight, Workshop, Controller, session, collision, combat and camera cases. Native/WASM simulation and combined combat continuation comparisons match.
- After the alternate model-renderer audio-only guard, all native cases/build-check were repeated and all three WASM applications rebuilt successfully. The final browser journey below uses those matching binaries.
- All eight signalling/real-transport journeys pass on the final quiet rerun, including fleet recovery, GM approval/replacement, verified WSS and lobby readiness. An earlier concurrent-build run passed six of eight while the GM and lobby journeys timed out; the final rerun resolves that verification failure.

Logs are retained in ignored `build/`: `combat-final-native.log`, `combat-final-web-gate.log`, `combat-final-presentation-web.log`, `combat-final-format.log`, and `combat-final-network-quiet.log`.

Synthetic combat fixtures load through production content, Controller admission, resources, Flecs, physical collision and save/restore. They cover arbitrary magazine counts; committed unload/load and return capacity; constant paid beams and solid cover; layered and fractional shield bypass; off regeneration, warm-up, funded sustain and inefficient low-rate battery redistribution; diagnosis and targeted structural repairs; impaired vector motion; elevated guidance and cover; seeded unguided flight; repeated-hit physical PDC interception and idle power; shared destruction chance/collateral, audio-only profiles and missile exclusion; exact continuation. Fixtures are independent of shipped ship definitions and tuning.

Save schema 9 records new commitments and rejects earlier runtime formats explicitly. Both native/WASM continuation fixtures exclude only synthetic archive identities from their comparison; each run still validates content identity on restoration.

## Manually invoked graphical evidence

The explicit native `phoenix_combat_smoke` target uses the authored duel and ordinary admitted Console actions. Its `mvp` journey opens/closes the system detail popup, enables Auto point defence and switches missile magazines while rendering flight. The support journey selects a compartment and assigns a travelling team; the destruction journey uses a synthetic child override to arrange a reproducible actual-fire destruction sequence.

All three native graphical journeys complete successfully on the corrected Console model. The support capture confirms selected-compartment/team assignment; destruction captures confirm growth, removal, fade and continuing session operation; the MVP captures confirm the accessible detail popup and ordnance presentation.

The final in-app browser journey used the real launcher to host Asteroid duel as Armed Courier, ready/start and render advancing WebGL combat. Settings and the rendered Back action were exercised; the camera wheel changed the view. At tick 1316 the same live duel displayed beam/tracer and shield-hit effects; inspected warning/error logs were empty. Public signalling failed in this environment, so this establishes local browser hosting/rendering rather than an off-machine crew connection. The final capture is `build/combat-mvp-browser.png`. An initial attempt during active compilation loaded mismatched JS/WASM; reloading after the completed build resolved it.

Captures remain in ignored `build/native`: `combat-system-popup.png`, `combat-tactical.png`, `combat-missile-flight.png`, `combat-scene.png`, `combat-support.png`, `combat-explosion-growing.png` and `combat-explosion-fading.png`. The popup provides readable status text and close/focus actions; the scene captures show missiles/smoke and shield effects. The existing Windows navigation protocol warning remains; it does not prevent the journey.

## Independent review

An independent agent compared production implementation and tests against the [original accepted plan](../combat-mvp.md). Six findings were fixed and re-reviewed: sub-energy shield conversion, timed unload, targeted structural repair, audio-only effects, basic-motion output faults and retained billboard orientation. A later compiler warning revealed a seventh issue in generated repair/weapon choices; it was corrected, covered through the Console action model and independently inspected. The reviewer found no remaining confirmed issue in the corrections. Review was read-only; gate results above are separately executed evidence.

## Acceptance limits

These runs establish implemented mechanisms and the exercised native/browser journeys. They do not establish weapon balance, actual screen-reader operation, physical touch/gamepads, audible quality, six-display performance or an off-machine browser crew journey. Finite repair supplies, shield retuning, post-launch retargeting and the new Cruiser encounter remain deferred by the accepted plan.
