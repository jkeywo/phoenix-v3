# Courier Patrol verification — 2026-10-19

`base.courier-patrol` ships alongside Free Flight in the base pack. Its single slot permits `base.courier-engineering`, retaining the existing Helm/Engineering ownership and Simple/Middle/Detailed presets. Two variants inherit that fit: the leader uses existing objective navigation, and the follower assigns the new contact-following tree to its engine and steering Systems. Engineering remains ordinary AI.

The four circular destinations form a 320 m × 200 m circuit with 18 m arrival regions. The leader cruises at the existing 9 m/s; the follower targets a point 40 m behind it with a 12 m/s requested velocity limit. Twelve existing asteroids are placed outside the observed turning corridor, including one in the circuit's empty centre. The existing emissive star is at (-500 m, -600 m), with scene lighting pointed in that direction. Players start at (0 m, 60 m), with no player objectives or automatic ending.

## Workshop authoring and persistence

Used the locally built browser Workshop at `http://localhost:8766/`. Imported a prepared draft, inspected its map and single-ship restriction, adjusted the player spawn and final star placement in Workshop, saved the installed pack, and exported verified ZIP copies. Reimported the export and reopened the saved base pack; Courier Patrol, the 60 m player spawn, asteroid layout and final star placement persisted. Free Flight remained selectable. Scene preview displayed the existing emissive star from an off-axis bearing.

The embedded browser left the native Save as picker pending. File → Export pack copy now uses the existing browser download path; its downloaded ZIP supplied the final map definition in `content/base/definitions.json`. The exported Courier variants are expressed as inheritance from the existing fit in source, avoiding duplicated equipment definitions. No existing base definitions were replaced.

## Executed checks

- Native and WASM `phoenix_scenario_tests`, `phoenix_scenario_world_tests` and `phoenix_gameplay_flight_tests`: passed. Synthetic production-path cases cover ordered actual region entry, 100 sequence legs with a one-objective/one-action limit, bounded receipts, pending completion recovery and continuation across cycle boundaries. Following cases cover permitted sensing, turning/speed changes, nearby stopped targets, unavailable power, lost/reacquired contacts, deterministic save/resume, and refusal of missing/self targets or invalid separation.
- Native `native` and browser `workshop`, `server`, `client`, and disposable Workshop worker: built successfully. The final base ZIP passed `phoenix_pack` production verification.
- Explicit authored-content smoke on the final map: 8,000 ticks (400 simulated seconds), reaching cycle 3, leg 2. Minimum Courier centre separation: 28.75 m. Minimum AI Courier-to-asteroid centre distance: 100.00 m. A preceding 12,000-tick tuning run completed five full cycles with a 28.70 m minimum separation. These are observations, not frozen tuning assertions.
- The smoke's independent control check claimed the player Helm and Engineering Consoles, selected Detailed operation, admitted Helm speed and Engineering generator commands, and refused cross-console control.
- Browser Workshop disposable Test ran beyond 12,400 ticks using the exported route, with empty diagnostics; Pause and Stop worked. This worker snapshot preceded the final visual star-position adjustment.
- Native graphical `--helm-smoke` on the final bundled pack passed direct throttle, heading, Settings Away/resume and advancing ticks. Captured Helm radar showed the two Couriers and surrounding asteroids.

Reproduce the optional authored-content probe (not part of CTest):

```powershell
build/patrol-native/Release/phoenix_pack.exe content/base tmp/patrol.zip
build/patrol-native/Release/phoenix_scenario_world_tests.exe --smoke-pack tmp/patrol.zip base.courier-patrol 8000
```

Local captured evidence: `tmp/courier-patrol-workshop.png`, `tmp/courier-patrol-browser-test.png`, `tmp/courier-patrol-helm.png`, `tmp/patrol-verified-smoke.jsonl`, and `tmp/patrol-native-helm-final.log`.

## Limits

Following requests paid propulsion and steering; it cannot guarantee separation without power or evade other traffic. It has no general obstacle avoidance. The authored clear corridor is intentional. The star is a small distant sprite and is visible when looking toward it. These checks do not establish physical multi-display, touch/gamepad or screen-reader acceptance, and no public deployment was performed.
