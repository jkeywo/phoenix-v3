# Play the local prototype

Build with `./scripts/build.ps1 -Target native`. The build produces `build/native/base.zip` from `content/base/` and a separate `build/native/training.zip` from `content/training/`, verified as a mod over the base. Fresh libraries enable the base and install training disabled. Enable Phoenix Training in Mod Packs for the authored smoke scenarios. Both packs can be toggled off; disabling the base also disables dependent mods. Synthetic integration fixtures remain independent.

Run `build/native/Release/native.exe --local-only` for offline play. Use Mod Packs to install, enable and order ZIP packs, then Host Game > New Game > scenario > ship. The stopped lobby offers local Console selection or AI crew and starts after readiness plus a five-second countdown. Settings and managed saves persist beneath `--data`; see the [native menu](native-menu.md). Native's default mode adds networked ship/crew/GM joining. Invalid pack selection or save import preserves the previous verified selection/session. The browser server retains its existing flat setup controls.

The Viewscreen fills its display region with the 3D rendering of the flat operating plane. It looks forward from the player ship, just above the plane, and follows actual movement and heading automatically. The owning ship is excluded. Only Settings appears over the world; the ordinary view has no orbit/pan/zoom, telemetry, Console shortcut or GM shortcut. Eight-way frame selection still uses camera bearing relative to each other entity's heading; scale is billboard height in metres.

Use a separate player client for an authored Console, or assign a Console to another native monitor in the lobby. The host remains the Viewscreen; the GM inspector can occupy another monitor. Consoles provide range-filtered radar, a vertical desired-speed slider and click/drag heading selection on the radar ring. Keyboard/screen-reader users can edit the desired heading numerically; controllers retain speed steps of 5% of fitted maximum and heading steps of five degrees. Radar is north-up; zero heading is east and 90 is north. Its textual report uses the same permitted contacts as the graphic. Shared semantic actions are published to browser buttons and Windows UI Automation; actual screen-reader operation remains an acceptance check.

After launch, Settings returns to the current display or explicitly quits to Main Menu. Quit ends the native session; start or join another game from the menu. There is no return-to-lobby button during play. Native/launcher Settings offers managed saving and ZIP export after a committed tick boundary. Import a save from the main menu to enter a paused recovery lobby, then ready up to resume. Settings does not pause simulation.

Runtime saves use the same bounded ZIP container as packs, with a `phoenix-runtime-save` manifest format, `session.json`, an inert `definitions.json` and ordered original pack bytes split into chunks of at most 8 MiB. Embedded pack hashes must match the saved simulation's ordered identities. Existing 32 MiB archive / 64 MiB expanded / 1024-entry limits also apply to the complete save; an oversized save reports an error. Runtime-save ZIPs cannot be imported as gameplay packs.

Imported content cannot be changed during play. Workshop opens independently from the main menu; there is no hot reload into a running session. For the browser, build with `./scripts/build.ps1 -Target web` and serve `build/web` over HTTP. Host Game opens the stopped lobby; player and GM clients join separately.

Manual native captures can be invoked with:

```powershell
./build/native/Release/native.exe --local-only --game-pack ./build/native/training.zip --mission training.navigation --slot training.player --ship training.survey --smoke
./build/native/Release/native.exe --local-only --game-pack ./build/native/training.zip --mission training.navigation --slot training.player --ship training.survey --console --smoke
```

Captures are written to the working directory as `viewscreen-smoke.png` or `console-smoke.png`. These are manually invoked smoke evidence, not automatic pixel tests or performance guarantees.

`native.exe --runtime-save path/to/phoenix-session.zip` verifies a runtime save and opens its paused recovery lobby. Add `--resume-local --console` to explicitly reclaim the local console on launch. This also accepts saves exported by server WASM; no separately installed content is needed.

The offline adapter supports independent local Console routes; use [display regions](console-displays.md) to show several at once. Default network sessions add durable singleton or fixed-roster fleet consensus, per-Console leases, approved crew and reconnection. Portable fleet recovery creates a fresh epoch; same-identity recovery uses the sealed journal and checkpoints. These implementations do not establish the six-screen/60 Hz or five-physical-device targets.

## Helm interaction update — 19 October 2026

Helm uses a centred, maximum-fit radar with a right-hand throttle and the shared settings cog. The marked diagnostic labels are hidden during ordinary play; keyboard focus still exposes the heading field and radar-contact action, and semantic controls retain their names. Away hands control to AI; Away and Resume control now live in Settings → Session for the Console that opened Settings.

Throttle clicks set the indicated position immediately. Captured dragging follows the track, clamps outside its travel, limits intermediate requests to 20 Hz and sends the final release position. Projection-driven slider updates suppress change events so they cannot feed commands back into the session.

Heading drags likewise limit intermediate requests to 20 Hz, suppress duplicate requests, and send the final release position. Updating the numeric heading from a projection cannot emit a new command. The dial and throttle replace redundant changing-value buttons in the Helm panel, and telemetry layout reads are batched before text updates to avoid repeated synchronous layout work.

Engineering presents permitted resource telemetry in system cards: heat and battery levels with capacity bars, reactor output, incoming/used power, power allocation, coolant allocation and remaining coolant. Its ordinary cards expose output, charging/discharging and allocation commands; automation choices are labelled "Use manual controls" and "Use automatic control". Additional admitted controls and reports remain below the cards. All commands use the existing Console model/session admission path. Add `--console --seat engineering --engineering-smoke` to an offline Courier launch to exercise telemetry, manual control, reactor/power/coolant adjustments and refusal to leave the station.

Eight-direction scene sprites retain their current view until five degrees past the nearest-frame boundary, including the wrap through zero. Engine attachments use the same retained hull frame. Legacy separate-image billboards load their eight images together instead of decoding a new image during a turn. The Viewscreen uses the same top-left settings cog as the Consoles and a thin transparent [generated frame](../content/ui/helm/viewscreen-frame.md).

Forward acceleration uses `2 × nominal × (1 − (current speed / maximum speed)²)`, with a 5% nominal floor to reach the cap. Braking retains the nominal constant rate. This applies through both basic and resource-paid flight paths. Viewscreen shake responds to changes in observed acceleration, decays quickly, is bounded, and is disabled by Reduced Motion.

Manual probe: add `--console --helm-smoke` to a local game launch. It exercises direct throttle positioning, radar heading, Settings Away/resume and continued simulation ticks, then writes `console-smoke.png`. On 19 October, the native 1280×720 Free Flight / training survey probe passed using a separate preview executable because the ordinary native executable was running. Native flight, motion/radar, session and input/semantics cases passed; browser client/server builds and the WASM flight case passed. The capture checks layout, not physical touch hardware, screen-reader usability or subjective shake tuning.
