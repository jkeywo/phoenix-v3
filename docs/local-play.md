# Play the local prototype

Build with `./scripts/build.ps1 -Target native`. The build produces `build/native/training.zip` from the editable starter content in `content/training/`, independently of the synthetic integration fixtures.

Run `build/native/Release/native.exe`. In Home or New game, add ZIP packs in dependency order. Mission, ship slot and ship buttons cycle through the resolved choices. Start local session creates the selected ship and claims its authored console through the host admission boundary. Invalid pack imports or setup leave the previous verified selection/session intact.

The viewscreen renders the full local server world with camera-facing or eight-direction billboards. Right-drag to orbit, middle-drag to pan and use the wheel to zoom. Eight-way frame selection uses the camera bearing relative to each entity's heading. Scale is billboard height in metres.

Open ship console uses the selected ship's authored widget definition: range-filtered radar, desired-speed steps of 5% of fitted maximum and desired-heading steps of five degrees. Radar is north-up; zero heading is east and 90 is north. Actual position, velocity magnitude and heading appear above the console. Radar's textual report uses the same permitted contacts as the graphic. Shared semantic actions are published to browser buttons and Windows UI Automation; actual screen-reader operation remains an acceptance check, not an established result.

Home returns to setup without stopping a session. End session and clear packs releases the running world and returns to content selection. Imported content cannot be changed during play. Workshop can still be opened independently; there is no hot reload into a running session.

For the browser, build with `./scripts/build.ps1 -Target web`, serve `build/web` over HTTP, and select Ship server in the launcher. Import the same training ZIP using the C++ home screen. The server WASM runs the local simulation. Console WASM and Workshop WASM do not link authoritative simulation; remote connection/claims remain pending.

Manual native captures can be invoked with:

```powershell
./build/native/Release/native.exe --game-pack ./build/native/training.zip --mission training.navigation --slot training.player --ship training.survey --smoke
./build/native/Release/native.exe --game-pack ./build/native/training.zip --mission training.navigation --slot training.player --ship training.survey --console --smoke
```

Captures are written to the working directory as `viewscreen-smoke.png` or `console-smoke.png`. These are manually invoked smoke evidence, not automatic pixel tests or performance guarantees.

This is one local server and one local console in a shared window, with no peer roster, network transport, consensus, QR admission, reconnect or portable runtime saves yet. It does not establish the six-screen/60 Hz or five-device targets. The server loop currently advances local fixed ticks; fleet play must replace that scheduler with committed tick frames before it can be enabled.
