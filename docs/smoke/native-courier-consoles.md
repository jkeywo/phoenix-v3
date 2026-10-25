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

## Integrated Helm follow-up

The 25 October follow-up removes Courier's duplicate Tactical surface and its layout subtabs. The same permitted Tactical model now supplies weapon toggles on Helm. Commands still use their original Console and Controller generation. Support does not offer Docking because its permitted docking board has no ports. One Systems popup holds statuses/details; engineering cards retain their dedicated statuses.

Advanced Radar projection was overwriting the authored radar range with the generic System maximum. Preserving the radar range restores detected contacts to their correct plotted positions, including the Rescue starbase. The synthetic paid-flight fixture checks its permitted radar range through production observation; the case without a radar feed remains restricted.

The mixed-resolution native graphical journey passed after these changes. It checks the missing Tactical/Support-Docking tabs, clicks a Helm weapon toggle and waits for the authoritative weapon enabled state to change, clicks the starbase marker and checks the selected-target panel, and opens/closes the Systems popup. The existing independent-page and stationary-launch checks also pass. Captures include `build/console-transition-helm-1080p.png`, `build/console-transition-target.png` and `build/console-transition-systems.png`. Five focused integration cases passed: gameplay_flight, sensors, input-and-semantics, content-and-packs and local-session. The native build check passed. This follow-up used the production offline lobby/background worker with virtual monitor regions; physical monitor and screen-reader acceptance is not claimed.

## Dedicated Comms follow-up

The 25 October Comms follow-up removes the Operations group from the shipped Comms layout and omits legacy non-conversation panels on conversation-only Consoles. Comms remains its own tab on the Support station. Transcript rows, message text and reply buttons use their container width instead of an early measured width and a fixed outgoing-message gutter. After authored layout, Comms controls use the measured transcript width converted from pixels to logical units. Nested percentage widths let the toolbar and replies shrink to intrinsic minimums when the display density changed.

The mixed-resolution native journey passed with a new check that Support's Comms tab publishes no machinery resource readouts; its capture is `build/console-transition-comms.png`. The explicit Comms journey now loads the demonstration from Test Missions with its current allowed Courier/Support setup. It checks transcript and reply widths at 1280 × 900 and 1920 × 1080, then exercises replies, the scripted participant joining, public transcript display and the narrow layout. The native build check and conversation, content-and-packs and input-and-semantics integration cases pass. Captures are graphical evidence from virtual window sizes, without physical-monitor or screen-reader acceptance.
