# Console displays and devices

The native window accepts `--display-layout path/to/layout.json`. Window coordinates are desktop pixels; region coordinates are relative to that window. Up to six non-overlapping regions can each claim an authored Console. Layout configuration does not create ships, Stations or Systems.

Alternatively, `--span-displays` creates one region per detected monitor, up to six, using the desktop arrangement. A supplied `--display-layout` takes precedence. The window is borderless and targets 60 Hz; this setting is not a measured performance result.

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

Each region owns an RmlUi context, document and headless Console model. The renderer offsets and clips each context. Touch contacts stay with their original region until release/cancellation; mouse capture also survives crossing regions. Clicking, touching or focusing an accessible control selects its keyboard region. Layout changes and focus loss cancel captured input.

Windows pointer events and browser Pointer Events feed RmlUi 6.2 touch events directly, with legacy synthetic mouse/touch suppression. Native controllers use SDL 3.4.16's controller subsystem, including HIDAPI and Windows Gaming Input, independently of raylib's four-slot controller API. Browser controllers use the Gamepad API. Choose **Assign controller** in a region explicitly. D-pad up/down changes requested speed, left/right changes heading, and the south button reads permitted radar contacts. Disconnect/reassignment releases the old route. Device IDs are connection-local; reconnecting requires reassignment.

Windows UIA controls expose their rendered bounds, enabled state and selected focus; UIA focus requests return through the application queue to the appropriate context. Browser semantic buttons share the canvas controls' geometry and translated labels, with a visible focus overlay. Radar labels include only the Console's permitted contacts. Pseudo-localisation applies to Console controls and authored widget labels; complete translation resources, complex-script shaping and IME support remain separate work.

Verification on 18 October 2026: native and WASM builds, eleven native integration checks, native UIA invoke probe, and single/two-region native rendered smoke. The attached controller was enumerated. This is not evidence of physical five-controller routing, simultaneous touchscreen contacts, real screen-reader usability or six-display 60 Hz performance. Those require manually triggered checks on the corresponding equipment; no new playable roster or ship content was introduced.

The available three-monitor setup was also probed: 1920×1200 at desktop x=-1920, 1920×1080 at x=0, and 1920×1080 at x=1920. Automatic spanning rendered and captured a 5760×1200 window after excluding a duplicate mirrored monitor reported by GLFW. Windows reported a maximum of 20 touch contacts, and SDL enumerated one controller. Physical tap/button interaction was not performed by this automated smoke.

Final focus checks on 18 October 2026 passed the native UIA label/bounds/focus/invoke queue probe. In Chromium, Tab advanced from Home to Join console to Viewscreen, and Enter opened Viewscreen with its visible focus outline. Native HTML controls retain browser keyboard behavior instead of being intercepted by GLFW; semantic focus does not disable page-level controller/touch routing. These checks do not substitute for a screen-reader user journey.
