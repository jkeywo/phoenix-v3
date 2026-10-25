# Console displays and devices

Display transition fix, 20 October 2026: loading uses the host monitor's region, including its desktop offset, for artwork, progress and Cancel hit/semantic bounds. Sidebar cleanup only restores its temporary crop; it no longer discards display assignments made during a draw frame. Native build and `system.input-and-semantics` passed. The explicitly invoked `--local-only --display-transition-smoke --data <isolated-directory>` journey passed with two virtual regions, host on the right; the captured running frame shows both the Console and host scene/sidebar. A separate five-frame loading capture confirmed the loading panel stays inside the offset host region. These are rendered virtual-region checks, not physical-monitor or touch acceptance.

The native window accepts `--display-layout path/to/layout.json`. Window coordinates are desktop pixels; region coordinates are relative to that window. Up to six non-overlapping regions can each claim an authored Console. Layout configuration does not create ships, Stations or Systems.

Alternatively, `--span-displays` creates one region per detected monitor, up to six, using the desktop arrangement. A supplied `--display-layout` takes precedence. The window is borderless and renders uncapped by default; use `--frame-limit N` or `--vsync` for explicit pacing. The workload target is six 1080p displays at 60 Hz; measured evidence is in [bridge smoke](smoke/bridge.md).

```json
{
  "x": 0,
  "y": 0,
  "width": 5760,
  "height": 2160,
  "regions": [
    { "route": 1, "x": 0, "y": 0, "width": 1920, "height": 1080 },
    { "route": 2, "x": 1920, "y": 0, "width": 1920, "height": 1080 },
    { "route": 3, "x": 3840, "y": 0, "width": 1920, "height": 1080 },
    { "route": 4, "x": 0, "y": 1080, "width": 1920, "height": 1080 },
    { "route": 5, "x": 1920, "y": 1080, "width": 1920, "height": 1080 },
    { "route": 6, "x": 3840, "y": 1080, "width": 1920, "height": 1080 }
  ]
}
```

Each Console region owns an RmlUi context, document and headless Console model. A region with `"role":"viewscreen"` instead renders the shared 3D scene; only one Viewscreen is supported. Region identities must remain distinct, including the Viewscreen. Automatic spanning assigns the first detected display to the Viewscreen and the others to Consoles. The renderer offsets and clips each context and uses the Viewscreen region's camera aspect. Touch contacts stay with their original region until release/cancellation; mouse capture also survives crossing regions. Mouse selection and accessible focus explicitly choose the keyboard region. Touch and controller input retain that selection. Layout changes and focus loss cancel captured input.

Windows pointer events and browser Pointer Events feed RmlUi 6.2 touch events directly, with legacy synthetic mouse/touch suppression. Native controllers use SDL 3.4.16's controller subsystem, including HIDAPI and Windows Gaming Input, independently of raylib's four-slot controller API. Browser controllers use the Gamepad API. Choose **Assign controller** in a region explicitly. D-pad up/down changes requested speed, left/right changes heading, and the south button reads permitted radar contacts. Disconnect/reassignment releases the old route. Device IDs are connection-local; reconnecting requires reassignment.

A native host can reserve one region with `"role":"gm"` for its read-only local inspector alongside the Viewscreen and Consoles. All roles count toward the six-region limit and require distinct routes. Clicking the inspector gives its tools keyboard focus; clicking a player Console or focusing a player control through UIA returns it. The pre-game lobby also lists detected monitors and offers **S2–S6** claim buttons in each panel for each available Console and for the GM inspector. The host's main monitor stays on the selected ship's Viewscreen. Console assignments reserve the corresponding claims before launch and use independent runtime routes. Assigned Consoles appear immediately on their monitors with the authored name, description and Ready/Back controls. Ready belongs to that local player; Back releases that screen assignment while the host remains in the lobby. Preview controls cannot command the ship before launch. Display roles remain fixed during play; there are no in-game Viewscreen/Console/GM switch buttons. Remote GM clients joining with the lobby code are admitted automatically; direct running-server GM invitations retain host approval.

Windows UIA controls expose rendered bounds, enabled state and selected focus; editable fields expose the Value pattern and dispatch Unicode updates through the application queue. Console action reports raise native notification events. Browser semantic inputs use ordinary HTML text editing/composition and forward their values to RmlUi. Radar labels contain only permitted contacts. English/pseudo resources are shared by player controls and static tool labels. The player renderer uses pinned HarfBuzz/FreeType with Arabic and Devanagari fallback fonts; native IME candidate placement follows the focused RmlUi input. See [MVP status](mvp-status.md) for the distinction between tested script samples and complete language support.

Verification on 18 October 2026: native and WASM builds, eleven native integration checks, native UIA invoke probe, and single/two-region native rendered smoke. The attached controller was enumerated. This is not evidence of physical five-controller routing, simultaneous touchscreen contacts, real screen-reader usability or six-display 60 Hz performance. Those require manually triggered checks on the corresponding equipment; no new playable roster or ship content was introduced.

The available three-monitor setup was also probed: 1920×1200 at desktop x=-1920, 1920×1080 at x=0, and 1920×1080 at x=1920. Automatic spanning rendered and captured a 5760×1200 window after excluding a duplicate mirrored monitor reported by GLFW. Windows reported a maximum of 20 touch contacts, and SDL enumerated one controller. Physical tap/button interaction was not performed by this automated smoke.

Earlier focus checks, before the forward-POV navigation change, on 18 October 2026 passed the native UIA label/bounds/focus/invoke queue probe. In Chromium, Tab advanced from Home to Join console to Viewscreen, and Enter opened Viewscreen with its visible focus outline. Native HTML controls retain browser keyboard behavior instead of being intercepted by GLFW; semantic focus does not disable page-level controller/touch routing. These checks do not substitute for a screen-reader user journey.

## Helm interaction

Throttle widgets use a vertical slider: up requests a higher speed, down a lower one. The track and grip use the original HUD PNGs and travel markers. A fitted heading widget makes the radar bezel a click/drag heading selector. Zero is east and 90 degrees is north; the orange pointer shows the request while the own-ship symbol and actual-heading text show the current response. The numeric desired-heading field supports keyboard and screen-reader editing. Controller speed/turn steps remain available through the same Console model.

Local Console controls remain active while the native host's primary screen is the Viewscreen. Mouse, captured touch and keyboard focus route to the individual RmlUi context; choosing a new display preserves the host's keyboard context until a player explicitly focuses another screen. See [Helm evidence](smoke/helm-console.md).

During native play, each player Console opens Settings inside its own RmlUi context. Multiple player overlays and the host's Settings can coexist; opening one does not change the other displays' screens. Escape follows the keyboard's assigned Console. Text size and language apply to that Console; audio explicitly controls the shared native output device. Away/Resume remains a per-Console Session action. Native player Consoles cannot release/change their station or quit to the menu; the host Viewscreen retains Save/Export/Quit. Browser station navigation remains unchanged.

Native worker frames include lobby-assigned local Consoles as well as direct claims. **Settings** opens Display preferences and contains Display, Language, Audio and Session. **Ship operations** contains stores/deliveries, docking, away missions, flight operations and Personnel; the Personnel and Away missions toolbar buttons open their corresponding pages directly. Overlays hide the underlying Console pages and tabs. See the [Courier launch regression receipt](smoke/native-courier-consoles.md) for the background-session reproduction and three-region checks.

See [GUI controls and presentation](gui-controls.md) for complete controller navigation, numeric editing, safe release, player text scaling and Windows provider identity. The direct flight bindings above apply in Flight mode.
