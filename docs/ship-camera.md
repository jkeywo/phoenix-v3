# Shared ship camera

Each ship owns one shared camera selection on its entity blackboard. Console GUIs control the ship's Viewscreen camera; they do not embed another camera image. Settings remain the only controls on the Viewscreen itself.

The initial selection is **First person / Forward**. First-person mode retains the existing shallow forward camera position and hides the owning ship. Third-person mode frames the ship from above and behind the selected viewing bearing and includes the ship in the scene. Chase distance and height scale with the ship's rendered size and sprite aspect ratio; there is no free orbit or zoom control.

Directions are clockwise relative to the ship: Forward, Forward-right, Right, Rear-right, Rear, Rear-left, Left, Forward-left. Each step is 45 degrees. They rotate with the ship's actual heading, rather than the world compass. In third person, Forward is the usual rear chase view; Rear moves the camera ahead looking back at the ship. Switching modes preserves direction. Selections change immediately; Reduced Motion still disables acceleration shake.

## Console authoring

Workshop's Console editor offers `camera_mode` and `camera_direction` widgets. Place them on any Console using the ordinary 1000-by-1000 authored layout. Their fixed bindings are `ship.camera.mode` and `ship.camera.direction`. They have no System, Controller or channel binding. Shipped Helm consoles include both widgets.

```json
{
  "id": "camera-mode",
  "kind": "camera_mode",
  "binding": "ship.camera.mode",
  "label": "Camera mode",
  "x": 0,
  "y": 100,
  "width": 180,
  "height": 160
}
```

Use the same structure with kind `camera_direction`, binding `ship.camera.direction` and a taller rectangle for the direction pad. Widget labels and choices use the normal translation resources. Workshop's isolated preview changes sample camera state without touching a running ship.

Select choices with pointer/touch or keyboard focus and Enter. On an assigned gamepad, North/Y enters camera focus, the D-pad moves between camera choices, South/A activates the focused choice, and East/B exits camera focus. Ordinary Helm gamepad commands retain their behavior outside camera focus. Selected choices are highlighted and identified in the semantic labels.

The widgets use generated weathered ivory/orange hardware matching the Helm. Direction arrows sit on the eight pads around the selector's ship silhouette; the mode rocker displays live `1P` and `3P` labels. Localized captions show the accepted selection, with amber selected-state highlights and a separate ivory keyboard-focus border. The artwork scales without changing the hit regions' bindings or semantic names.

## Authority and persistence

The reserved `phoenix.camera` ship record has integer `mode` (0 first-person, 1 third-person) and `direction` (0 through 7 in the order above). Its producer is the trusted ship runtime. The normal committed command path accepts `camera_mode` or `camera_direction` only from an admitted operator whose Console contains that widget. These settings require no engine/steering capability and do not override a physical Controller. Forged physical bindings, unsupported values and cross-ship requests are refused.

All viewers and permitted Consoles consume the accepted blackboard projection. Pending requests do not become selected states. Updates to different fields preserve each other; conflicting updates follow committed command order. Operator departure, save/recovery and network reconnection retain the shared selection. Older saves without this built-in schema receive the initial selection; unrelated schema changes and malformed camera records still fail restoration. Camera state is retained ahead of expendable report history in bounded network projections.

See [Console presentation](gameplay-consoles.md) and [local play](local-play.md) for the surrounding input and session workflows.

## Verification and manual smoke

The synthetic `phoenix_camera_tests` target covers admission, shared updates, ship isolation, save compatibility, recovery, bounded projection and camera geometry. It runs as `system.ship-camera` under native CTest and as `phoenix_camera_tests.js` in the WebAssembly check. The live-session probe also exercises remote camera commands and selection retention when an operator is replaced.

Build the optional `phoenix_camera_smoke` target to run the graphical check manually. On native builds, run it from an output directory with the pack directory as its first argument; add `portrait` for a 1080-by-1920 window. It checks gamepad navigation, widget bounds, non-overlapping choices and modal admission, then captures both modes in all eight directions. For example, from `build/linux`, run `./phoenix_camera_smoke .` (use `xvfb-run -a` on a headless Linux machine).

On WebAssembly, serve the build directory over HTTP and open `phoenix_camera_smoke.html`, optionally with `?portrait`. The page displays its result; `Module.cameraCaptures` contains the PNG data URLs for the console and all sixteen views. This smoke target is excluded from normal builds and automated CTest. These checks do not establish physical gamepad, touch or screen-reader acceptance.

Verified on 20 October 2026: the Linux gate passed 22 CTest cases, 10 launcher cases and 8 live network cases; all 19 WASM checks passed, with matching native/WASM continuation traces. Manually invoked native OpenGL and Chromium WebGL runs passed the camera controls, semantic selection, gamepad activation, bounds and modal checks in landscape and 1080-by-1920 portrait, capturing all sixteen views. The synchronous browser smoke can emit an AudioContext resume warning after closing its window; its camera assertions and captures complete before that teardown.
