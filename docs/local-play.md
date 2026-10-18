# Play the local prototype

Build with `./scripts/build.ps1 -Target native`. The build produces the default `build/native/base.zip` (also emitted as the identical `training.zip` compatibility alias) from the editable starter content in `content/base/`, independently of the synthetic integration fixtures.

Run `build/native/Release/native.exe --local-only` for offline play. Use Mod Packs to install, enable and order ZIP packs, then Host Game > New Game > scenario > ship. The stopped lobby offers local Console selection or AI crew and starts after readiness plus a five-second countdown. Settings and managed saves persist beneath `--data`; see the [native menu](native-menu.md). Native's default mode adds networked ship/crew/GM joining. Invalid pack selection or save import preserves the previous verified selection/session. The browser server retains its existing flat setup controls.

The Viewscreen fills its display region with the 3D rendering of the flat operating plane. It looks forward from the player ship, just above the plane, and follows actual movement and heading automatically. The owning ship is excluded. Only Settings appears over the world; the ordinary view has no orbit/pan/zoom, telemetry, Console shortcut or GM shortcut. Eight-way frame selection still uses camera bearing relative to each other entity's heading; scale is billboard height in metres.

Use a separate player client for an authored Console, or select **Display: Player console** in the native lobby before launch. Native can instead select Viewscreen or GM inspector there; configured multiple-screen layouts retain their separate roles. Consoles provide range-filtered radar, desired-speed steps of 5% of fitted maximum and desired-heading steps of five degrees. Radar is north-up; zero heading is east and 90 is north. Its textual report uses the same permitted contacts as the graphic. Shared semantic actions are published to browser buttons and Windows UI Automation; actual screen-reader operation remains an acceptance check.

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
