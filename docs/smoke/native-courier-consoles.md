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

## Mixed-resolution follow-up

The follow-up probe uses a 5760 × 2160 window with a 3840 × 2160 host and two separate 1920 × 1080 station regions. The previous probe's 1280 × 720 regions did not exercise density scaling. Measured RmlUi pixel dimensions were being written back as density-independent units, enlarging instruments a second time. Instrument/aspect placement now converts pixels back to logical units before applying styles.

Console, layout and common gameplay pages use one station tab strip. Pointer probes pass through RmlUi mouse events and custom pointer interception, switching Helm/Tactical and Support/Comms/Navigator. Buttons take precedence over radar input. The probe checks that radar bounds stay inside each station, initial speed and velocity are zero, and tab navigation does not request motion. Machinery cards and generic system diagnostics include only the current Console's Controller consumers, keeping Support machinery off Helm.

Station preparation now publishes a local loading readout and overlay on newly assigned displays, while preserving host lobby controls. The graphical journey checks that local readout and readies/unreadies a station through the live host controls while loading is visible. Its loading capture is `build/console-transition-local-loading.png`; the individual station capture is `build/console-transition-helm-1080p.png`. Offline and private-loopback hosted mixed-resolution journeys passed.

`system.native-menu-library` checks monitor preference persistence. `system.lobby-worker-consoles` restores a saved monitor after its index changes, refuses to redirect a station when that monitor is missing, and exercises launch/release through the worker. Windows saves device interface identities; synthetic checks do not establish physical unplug/replug behavior. These changes have native graphical and synthetic verification; browser, physical touch/gamepad and screen-reader acceptance remain separate.

All five focused system cases passed. A final invocation of `system.native-menu-library` encountered one Windows atomic file-replacement failure; its isolated retry passed. The native build check passed. No physical mixed-monitor or screen-reader acceptance is claimed by these virtual-region captures.
