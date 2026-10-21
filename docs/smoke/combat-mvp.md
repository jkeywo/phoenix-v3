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

## Combat audit cleanup verification — 21 October 2026

All eight follow-up recommendations in `docs/combat-mvp.md` are implemented in separate ordered commits on `codex/combat-mvp`. An independent read-only review found no remaining actionable gaps or confirmed regressions. It also independently compared expanded Courier/Raider equipment with the previous fit after normalizing the new resource namespace; authored values match.

The complete native build and all 23 native system cases pass. The complete WASM build and web system gate pass, including native/WASM continuation comparisons. Native production build-check, base-pack production verification and repository formatting checks pass. Clang exposed dependent-template syntax accepted by MSVC during the first web build; the correction was rebuilt and checked on both platforms.

The explicitly invoked native `mvp`, `support` and `destruction` graphical journeys all pass on the final refactored build. The status popup capture was visually inspected; the probes exercise status popup close/focus, automatic PDC, ammunition selection, team assignment and physical destruction growth/removal/fade. Updated captures and probe logs remain under ignored `build/native`. The rebuilt worktree game was reopened after these probes.

Current combat continuation uses schema 10. Schema 9 and earlier saves are explicitly refused; start a new session. This supersedes the original implementation's schema-9 format recorded above.

The final quiet real-transport gate passes all eight journeys, including fleet recovery, GM approval/replacement, verified WSS and lobby readiness/countdown.

Audit logs remain in ignored `build/`: `audit-final-native-build.log`, `audit-final-native-rebuild.log`, `audit-final-native-tests.log`, `audit-final-build-check.log`, `audit-final-web-build.log`, `audit-final-web-gate.log`, `audit-final-format.log` and `audit-final-network.log`.
