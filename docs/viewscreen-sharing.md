# Viewscreen sharing

Radar and navigation widgets expose **On screen** during play. Pressing it shows the selected view live over the ship’s normal world scene, dimmed with a 65% black layer. Navigation and other radar layouts use a bevelled instrument housing, monitor symbol and status lamp, with **Live** and **Standby** captions. All variants show a bright green lamp while active and a dark lamp while inactive. On Helm the control is an external pod below the radar's lower-right edge, with two metal mounting arms joining it to the casing. Its horizontal 22px label, status caption and recessed lamp sit outside the frame and the heading arrow's full sweep. The pod has a chamfered pointer target; the heading ring remains usable. It exposes its pressed state through browser accessibility and Windows UI Automation. Localisation, keyboard, gamepad and touch use the ordinary Console action path.

A different source replaces the current one. Pressing the currently displayed source again restores the normal Viewscreen. Sources are never stacked or queued. Navigation follows the publisher’s current pan, zoom, objectives and waypoint. Radar uses the selected sensor’s permitted contacts, envelope, marker filters and authored sweep appearance. The Viewscreen has no specialist controls; Settings and shared messages remain available. Equivalent map readouts are exposed to assistive technology without filling the visual overlay with coordinate lists.

## Commands and projection

`display.toggle` and `display.update` travel through the existing authenticated OperatingCommand, Console lease and committed-frame ordering. `console` identifies the source Console and `system` identifies its authored widget. The three integer arguments are camera centre X/Y in millimetres and horizontal extent in decimetres; radar uses a placeholder camera. Only explicit toggle commands change priority. A live update must match the current publication’s revision, owner and widget, including when it was queued before a replacement.

Each ship owns its publication. The public board’s optional `display` member contains only selected visual data: source identity/revision, camera, own ship, selected radar returns/envelope/sweep, supported region markers, objectives and waypoint. Both network boundaries reconstruct an explicit allowlist. The visual envelope is bounded to 1 MiB; exceptionally crowded radar feeds retain whole contact entries and report an explicit omission count. Console command sections, machinery telemetry, private knowledge and addressed inboxes are absent. Remote commands retain the selected supplemental Console address through client filtering and host consensus frames.

Release, disconnection or reassignment ends the outgoing publisher’s sharing. Replica checkpoints retain the active publication for deterministic continuation; portable save recovery clears it together with transport claims. New sessions and recovered games start with the normal Viewscreen. New viewers receive the current live publication through the ordinary public projection.

## Validation

Synthetic Controller/runtime integration covers source admission, selected-sensor isolation, ship isolation, replacement, same-source cancellation, queued stale updates, live navigation camera changes, projection filtering, semantic toggle state, checkpoint continuation and portable recovery. The live lobby integration covers remote radar publication, replacement by a supplemental navigation Console, live camera updates and cancellation through the real WSS/relay session path.

The explicit native graphical journey is separate smoke evidence:

```powershell
./build/native/Release/phoenix_scenario_world_tests.exe --smoke-gameover build/native/base.zip base.courier-race 150 build/on-screen-race.zip
./build/native/Release/native.exe --local-only --runtime-save build/on-screen-race.zip --resume-local --console --data build/on-screen-smoke-userdata --display-layout build/on-screen-layout.json --frame-limit 60 --on-screen-smoke
```

For the capture, `on-screen-layout.json` configures a 1800 × 900 window at desktop (20,20), with a Console region at (0,0,900,900), route 1, and a Viewscreen region at (900,0,900,900), route 2. The journey presses the actual routed actions, switches from Helm radar to navigation, pans/zooms, toggles sharing off and writes `build/on-screen-radar.png`, `build/on-screen-navigation.png` and `build/on-screen-normal.png`. These artifacts are ignored local evidence. This virtual two-region run does not establish six physical displays, physical touch/gamepad operation or actual screen-reader usability.

### Evidence — 20 October 2026

The feature build on baseline `d96eb51` passed all 21 native integration cases, the complete `scripts/check.ps1 -Target web` gate (including the native/WASM continuation comparison), and all three browser application builds. Formatting verification also passed. The network-service run passed seven cases; the extended lobby fixture initially failed an old first-record-only assumption after adding its public waypoint. The corrected assertion preserves the public/private boundary, and its focused WSS/relay rerun passed with remote radar, supplemental navigation, camera update and cancellation checks.

The explicitly triggered graphical run passed all three display assertions, and the radar, navigation and normal captures were visually inspected. Configuration: Windows native Release, raylib GLFW/Win32, Intel Graphics with GLSL 3.30 (driver 32.0.101.8628), two virtual 900 × 900 regions in one 1800 × 900 window, 60 Hz frame limit, Courier Race save at tick 150. Actions used the production routed semantic controls. The navigation capture shows the live world still visible behind the dimming layer; the normal capture confirms that both the map and dimming disappear. No physical touch, gamepad or screen-reader journey was performed.

The subsequent control-styling check passed native and browser-client builds and refreshed all three graphical captures. The opt-in journey now sends pointer clicks through custom-dial interception and RmlUi hit testing for the On screen control, confirming that the mounted button does not change heading. The active green lamp and inactive Standby appearance were visually inspected. This remains virtual-pointer evidence rather than physical touch or screen-reader acceptance.

The bezel-integration revision passed native and browser-client builds and refreshed the graphical journey. Its pointer probe also checks that an unused bounding-box corner does not activate sharing and that releasing outside the curved control cancels a press. The inset label, focus outline and active/inactive lamp were visually inspected. The RmlUi backend now applies element transforms so the localised label can follow the bezel.

The external-pod revision passed native and browser-client builds and refreshed the sharing journey and idle Helm capture. The pointer probe checks clearance around the entire heading-arrow sweep, rejects the pod's cut corners and cancels presses released outside. Active and idle captures were inspected for label size, lamp state, mounting-arm placement and clearance from the radar and throttle controls.
