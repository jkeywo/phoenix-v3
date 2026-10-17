# Play the local prototype

Build with `./scripts/build.ps1 -Target native`. The build produces `build/native/training.zip` from the editable starter content in `content/training/`, independently of the synthetic integration fixtures.

Run `build/native/Release/native.exe --local-only` for this offline adapter. In Home or New game, add ZIP packs in dependency order. Mission, ship slot and ship buttons cycle through the resolved choices. Start session creates the selected ship and claims its authored console through the host admission boundary. Invalid pack imports or setup leave the previous verified selection/session intact. Native's default mode now supports [network play](network-play.md), with an explicit local or remote station claim.

The viewscreen renders the full local server world with camera-facing or eight-direction billboards. Right-drag to orbit, middle-drag to pan and use the wheel to zoom. Eight-way frame selection uses the camera bearing relative to each entity's heading. Scale is billboard height in metres.

Open ship console uses the selected ship's authored widget definition: range-filtered radar, desired-speed steps of 5% of fitted maximum and desired-heading steps of five degrees. Radar is north-up; zero heading is east and 90 is north. Actual position, velocity magnitude and heading appear above the console. Radar's textual report uses the same permitted contacts as the graphic. Shared semantic actions are published to browser buttons and Windows UI Automation; actual screen-reader operation remains an acceptance check, not an established result.

Home returns to setup without stopping a session. Save runtime ZIP waits for the next local tick boundary and exports the simulation state plus the exact original selected pack archives. Load runtime save verifies all embedded packs and state before replacing the current session; a failure preserves the running world. Recovery pauses in the server lobby with no live claim. Reclaim local console and resume explicitly creates the local claim and continues saved motion, requests, integer remainders and delegated work. This local recovery has no transport identity or remote station replacement yet.

Runtime saves use the same bounded ZIP container as packs, with a `phoenix-runtime-save` manifest format, `session.json`, an inert `definitions.json` and ordered original pack bytes split into chunks of at most 8 MiB. Embedded pack hashes must match the saved simulation's ordered identities. Existing 32 MiB archive / 64 MiB expanded / 1024-entry limits also apply to the complete save; an oversized save reports an error. Runtime-save ZIPs cannot be imported as gameplay packs.

End session and clear packs releases the running world and returns to content selection. Imported content cannot be changed during play. Workshop can still be opened independently; there is no hot reload into a running session.

For the browser, build with `./scripts/build.ps1 -Target web`, serve `build/web` over HTTP, and select Ship server in the launcher. Import the same training ZIP using the C++ home screen. The server WASM runs authoritative simulation through durable committed frames; claim its local console or connect a remote client. Console WASM and Workshop WASM do not link authoritative simulation.

Manual native captures can be invoked with:

```powershell
./build/native/Release/native.exe --local-only --game-pack ./build/native/training.zip --mission training.navigation --slot training.player --ship training.survey --smoke
./build/native/Release/native.exe --local-only --game-pack ./build/native/training.zip --mission training.navigation --slot training.player --ship training.survey --console --smoke
```

Captures are written to the working directory as `viewscreen-smoke.png` or `console-smoke.png`. These are manually invoked smoke evidence, not automatic pixel tests or performance guarantees.

`native.exe --runtime-save path/to/phoenix-session.zip` verifies a runtime save and opens its paused recovery lobby. Add `--resume-local --console` to explicitly reclaim the local console on launch. This also accepts saves exported by server WASM; no separately installed content is needed.

The offline adapter is one local server and one local console. Default network sessions add transport, durable singleton consensus, station leases and reconnection. Neither establishes the six-screen/60 Hz or five-device targets. Portable saves cover one server's selected world, not recovery of a networked fleet.
