# Native Courier console launch — 25 October 2026

The background session published observations only for its primary route and explicit `claim_console` calls. Native display assignments use lobby commands; the lobby claims those seats internally at launch. Both Courier surfaces therefore retained their Settings/Personnel/Away toolbar but received no gameplay observation. A manual reproduction through the production offline lobby and worker showed exactly those empty framed Consoles.

Worker publication now includes the current lobby's local screen reservations alongside explicit routes. The synthetic `system.lobby-worker-consoles` case loads the test-only lobby fixture through the production loader and native worker, moves a display reservation, readies it and checks its immutable Console observation after launch. Its former route stays absent, and releasing the Console removes its observation.

Player overlays separate preference tabs from gameplay tabs. Settings always opens Display; Ship operations opens stores/deliveries, with docking, away missions, flight operations and Personnel alongside. Personnel and Away missions open their own gameplay pages. Overlay content uses density-independent width, and the underlying authored pages, Console tabs and navigation are hidden while the overlay is open.

## Explicit graphical journey

Build from the repository root, then run either offline or with a private signalling service:

```powershell
./.tools/python/cmake/data/bin/cmake.exe --build build/native --config Release --target native phoenix_console_transition_smoke
./build/native/Release/phoenix_console_transition_smoke.exe build/native
./build/native/Release/phoenix_console_transition_smoke.exe build/native ws://127.0.0.1:19086/phoenix
```

The probe is excluded from the default build and CTest. It loads the production Courier and Rescue, assigns Helm and Support through the real lobby, readies both, waits for launch and graphics preparation, and checks visible gameplay controls on both independent routes. It opens Personnel, returns to Settings, changes text size, and opens Away missions through ordinary presentation actions. It checks that preference/gameplay tabs remain separate and the other Console stays usable.

On this Windows machine (Intel Graphics/OpenGL 3.3), both offline and private-loopback network-hosted runs passed with the normal background session. The 3840 × 720 window contains three virtual 1280 × 720 regions: Viewscreen, Helm and Support. Captures were inspected at `build/console-transition-running.png`, `build/console-transition-personnel.png`, `build/console-transition-settings.png` and `build/console-transition-away.png`. Before the worker fix, the same probe failed with zero visible gameplay controls and captured the empty framed Consoles. Afterward, both routes expose gameplay controls and the overlay journey completes.

The native build check and `system.lobby-worker-consoles`, `system.local-session`, `system.input-and-semantics` and `system.content-and-packs` pass. These checks do not establish physical three-monitor input, touch/gamepad operation, screen-reader usability or six-display performance.
