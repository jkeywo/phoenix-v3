# Courier Race and Navigation

Courier Race is a base-pack map with two playable Race Couriers, a shared finish area and asteroid obstacles. Both shipwide objectives activate at simulation tick 100 (five seconds after play starts). First arrival ends the race; arrivals in the same tick draw. The existing swept centre-entry rules apply. Results are durable objective records: the winner completes its objective, the runner-up fails it, and each crew sees its own Won/Lost/Draw screen. The physical world freezes after resolution while committed frame numbering continues.

## Reused runtime

The map uses the existing Courier fitting, asteroid definition, scenario evaluator, reach-region geometry, knowledge publication, Controller command admission and console rendering. `competition` on an objective groups distinct actors aiming at the same region. Competition objectives cannot be repeated sequences. The runtime resolves all arrivals together before granting credit; an author cannot command successful completion.

`phoenix.objective` now publishes description, outcome and region radius alongside its existing fields. An `objectives` widget bound to `ship.objectives` displays the ship's objective list. Existing general readouts also expose these records. Old ordinary objective saves default to no competition outcome; changed Controller programs retain the existing signature compatibility checks.

## Human-seeking consoles

Any console definition can declare `human_seeking: true`. Its ship placement may declare `human_preferences`, an ordered list of station IDs. The default order is its home station followed by the ship's other authored stations. The console follows the first available human; it does not require another lobby seat. Navigation prefers the Courier's Engineering/support player, then Helm.

The selected player receives additional permitted Console projections and can switch between them without releasing their primary seat. Commands carry the selected Console address and its Controller generation. Joins, departures and Away changes recompute ownership; stale writes fail. With no human available, ordinary Controller AI/hold fallback applies. Knowledge and accepted settings survive handover.

## Map and waypoint

The `navigation` widget binds to `ship.navigation` and a Controller's `waypoint` channel. It displays a north-up grid, own ship, objective areas and waypoint. Initial fitting and Reset view include the current ship, published objective areas and active waypoint. Manual pan/zoom suspends automatic fitting until Reset view.

Drag to pan, use the wheel or pinch to zoom, or use the labelled controls. Click/tap to set a waypoint; select its marker or Clear waypoint to remove it. Objective buttons and coordinate inputs provide equivalent non-pointer actions. Map view state is local presentation state. **On screen** publishes its live pan/zoom to the ship’s Viewscreen as a read-only overlay; radar has the same toggle. The latest source replaces the previous one, and the displayed source’s next press restores the normal view. See [Viewscreen sharing](viewscreen-sharing.md).

The waypoint channel is a consumer-free setting with a boolean value and two integer millimetre arguments. Each ship has at most one such channel. Accepted intent is published as `phoenix.waypoint` with coordinates, validity, revision and arrival state. `arrival_radius_mm` defaults to 5000 and is authored on the channel. Physical entry marks that revision reached without removing the marker. Helm draws an in-range blip or an edge arrow and exposes distance/bearing text.

## AI

Race helm trees select an unreached manual waypoint before the active objective. Priority is authored behavior. Local clearance observations use only permitted radar contacts, own motion and conservative body bounds. When blocked, the tree stops forward demand and turns left until clear, advances briefly, then rechecks its destination. The existing physical command path still owns acceleration, steering, power and collisions. This is local avoidance, not global route planning.

## Validation

Courier lobby loading validates directly claimable seats; human-seeking Navigation follows an existing operator and is not admitted as a separate seat. The synthetic fleet-setup case covers this distinction. To verify current authored scenarios through ship and GM setup, run `phoenix_fleet_setup_tests --verify-pack build/native/base.zip` from the repository root (using the executable in `build/native/Release`).

Synthetic tests cover same-tick competition resolution, delay/recovery, generic console assignment and stale generations, waypoint publication, map fit/zoom/reset and exclusion of unseen obstacles from clearance. Shipped race runs are opt-in smoke evidence, not tuning assertions. Physical touch, screen-reader usability and six-screen performance require their own manual acceptance runs.

Verified on 20 October 2026: native and all browser role builds; all 21 native system cases and 18 WASM checks; matching native/WASM synthetic continuation; signalling, fleet recovery, live Console routing and verified native WSS checks. The authored race smoke issued both objectives at tick 100 and resolved a winner and loser at tick 994. These tick results record this run, not a balance assertion.

The native report capture (`build/mission-report.png`) shows live green monospace text on the [generated CRT surround](mission-terminal-art.md). The navigation capture (`build/navigation-map.png`) follows console switching, waypoint set/clear, zoom, pan and reset through the production UI. These captures are manually invoked and are not CI gates.

To reproduce the previews after building:

```powershell
./build/native/Release/phoenix_scenario_world_tests.exe --smoke-gameover build/native/base.zip base.courier-race 1300 build/race-finished.zip
./build/native/Release/native.exe --local-only --runtime-save build/race-finished.zip --resume-local --console --data build/race-preview-userdata --smoke
./build/native/Release/phoenix_scenario_world_tests.exe --smoke-gameover build/native/base.zip base.courier-race 150 build/race-navigation.zip
./build/native/Release/native.exe --local-only --runtime-save build/race-navigation.zip --resume-local --console --data build/race-navigation-userdata --navigation-smoke
```

Each native command writes `console-smoke.png`; preserve it before running the next capture. The probe saves the world at the requested tick, so the shorter run provides an active race for navigation inspection.
