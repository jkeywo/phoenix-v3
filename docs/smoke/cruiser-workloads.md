# Cruiser workload implementation checks — 23 October 2026

The [workload contract](../cruiser-complexity.md) covers Helm, Tactical and Engineering. Courier fits and the remaining Cruiser workload allocations retain their existing behavior. World saves now require schema 20; mission continuation uses schema 11. Older authored single-Dispatch and single-transport missions remain supported.

Executed on Windows with a native Release working-tree build, runtime implementation through `6d4e3ef` and explicit journey harnesses through `7905488`. The graphical run used one 1920×1080 window and programmatically activated the ordinary Console controls. Concurrent psychic/mission work was retained in the shared workspace.

## Automated evidence

- Native Release build and startup check; 37 CTest cases. Includes host admission, local/network projections, controller handover, physical flight/combat, repair, personnel, logistics, docking, missions and input/semantics.
- Production root plus Test Missions content verified: 35 entity definitions, eight maps and 20 Consoles.
- Added synthetic cases use the production loader, command admission, simulation and restore paths: per-Console defaults, shared commands and overrides, coordinated impulse, compartment/system/hull repair, dedicated stock authority, department Dispatch, craft-only missions, escorts and shared reservations, gift/barter quantities, shortages, cancellation, destruction and continuation.
- The service/network gate passed all 12 cases, including actual fleet recovery, content transfer, lobby operation and trusted/untrusted native WSS. The launcher passed 27 cases.
- Full WebAssembly build; all 33 WASM suites and the production worker launch/content preflight passed. All 15 native/WASM continuation traces matched: simulation, combat, controller runtime, conversations, psychic, sensors, medical, logistics, docking, cargo, Security, away missions, craft, FTL and mission lifecycle.

These checks compare continuation within the same run and across native/WASM; they do not freeze shipped balance values.

## Explicit production-fit journeys

`phoenix_cruiser_smoke base.zip --watch 1000 test-missions.zip` occupied Tactical and Engineering at one star. Its private fixture seeded 100 engine-compartment hull damage. Player target selection produced actual weapon fire and automatic combat sorties; the player assigned a repair team to the engine compartment, whose hull improved from 1900 to 1999 while combat continued. Mid-combat restoration continued identically. This bounded watch is separate from a complete three-wave balance exercise.

`phoenix_cruiser_smoke base.zip --supply test-missions.zip` used a quiet combat-map variant with the depot 600 metres ahead and initially depleted parts/medical stores. Comms accepted both offers, Engineering opened and dispatched them at one star, and Tactical authorized an automatically selected interceptor escort. The gift physically delivered 200 parts. The barter physically paid 200 parts and delivered 100 medical supplies. Both shuttles returned automatically, and completion waited for home unloading. Each observed transport/exchange phase was restored and compared before continuation. Quantities are execution observations, not tuning assertions.

The local receipts are `build/cruiser-watch-receipt.json` and `build/cruiser-supply-receipt.json`; the latter retains the resulting departmental mission projections. An earlier long-approach checkpoint is retained separately under `build/` and is not completion evidence. The existing transport/boarding helpers were updated for departmental ownership and compiled, but those separate journeys were not rerun for this change.

## Console and device scope

`phoenix_cruiser_ui_smoke base.zip test-missions.zip --workloads` passed for Helm, Tactical and Engineering separately at 1920×1080 for all three star levels. The quiet UI variant also starts with depleted stores. It exercises the rendered Comms response, Engineering preparation/dispatch and Tactical contribution buttons. Nine workload captures, three conversation/contribution/dispatch captures and `build/cruiser-ui-dispatch-receipt.json` record the views and admitted ownership/manifest state. The dispatched view reports Engineering ownership, its utility transport, the Tactical interceptor contribution and physical loading status.

Assisted Engineering views foreground available decisions rather than passive machinery cards. Tactical retains its radar and grouped firing controls. Mission panels identify the owning Dispatch, department contribution, primary transport, escort, manifest, payment, readiness blocker and delivery status.

Automated input/semantic-control checks and these explicitly invoked graphical journeys are separate from hands-on device acceptance. Physical screen-reader use, touch/gamepad hardware, browser IndexedDB reload and six-display performance still require their respective device runs. No such hardware acceptance is claimed here.

## Repeating the checks

Build `phoenix_cruiser_smoke` and `phoenix_cruiser_ui_smoke` explicitly; both depend on the production root and Test Missions packs. Run the commands above from the repository root, supplying the generated pack paths. Keep native smoke executables closed when rebuilding shared asset copies on Windows.

This run used `build/courier-native` and `build/courier-web`. Native and browser root/child pack hashes matched. Logs are `build/cruiser-complete-native-build.log`, `build/cruiser-complete-native-tests.log`, `build/cruiser-complete-ui.log`, `build/cruiser-complete-web-build.log`, `build/cruiser-complete-web-tests.log`, `build/cruiser-network-tests.log`, `build/cruiser-launcher-tests.log`, `build/cruiser-watch.log` and `build/cruiser-supply-final.log`. Build artifacts and captures remain local execution evidence rather than tracked content goldens.
