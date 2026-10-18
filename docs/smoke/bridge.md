# Native bridge evidence — 18 October 2026

Manually triggered synthetic workload, not a balance assertion or an automatic performance gate. Fixtures come from `tests/manual/bridge_fixture.py`: one Viewscreen, up to five independent claimed Consoles and 100 synthetic contacts. Capture waits 120 warm-up frames and records 1,800 frames with every configured Console active.

Reference machine: Core Ultra 9 275HX, NVIDIA RTX 5090 Laptop GPU, driver 617.14, OpenGL 3.3. Actual desktop: left 1920×1200, centre and right 1920×1080. The native window spanned all three. Rendering used the default uncapped bridge mode; the 60 fps software limiter produced periodic graphics-flush stalls in comparison runs.

| Workload              | Framebuffer | Consoles | Frame p50 | Frame p95 | Frame p99 | Maximum  |
| --------------------- | ----------- | -------- | --------- | --------- | --------- | -------- |
| Actual three displays | 5760×1200   | 2        | 6.90 ms   | 9.18 ms   | 9.52 ms   | 11.93 ms |
| Six 1080p regions     | 5760×2160   | 5        | 7.02 ms   | 9.93 ms   | 10.31 ms  | 13.29 ms |

Both captures recorded 1,800 focused frames. Simulation/poll p95 was below 0.07 ms; five-Console presentation p95 was 0.76 ms. These are CPU wall-clock frame measurements including presentation, not isolated GPU timestamps. The six-region image was inspected for the populated Viewscreen and five radars. Six physical monitors and their independent scanout were unavailable, so this establishes rendering workload headroom on this machine, not certification of every six-monitor installation.

Raw JSON, logs and PNGs remain under ignored `build/bridge-smoke/three-final.*` and `six-final.*`. Recreate with the commands in [MVP status](../mvp-status.md). `--frame-limit N`, `--vsync` and `--uncapped` allow explicit pacing comparisons.

## Input and accessibility

Windows enumerated the Xbox One controller. Synthetic integration checks exercise device assignment, captured pointer routing and focus isolation, including touch not taking keyboard ownership. Physical controller button presses remain a manual acceptance item. Touchscreen calibration and physical touch checks are deferred at the user's request.

The native platform probe passed control names, bounds, focus, invocation and Unicode editable Value access through the production UIA provider. Browser DOM controls and Unicode editing were inspected. Native/browser Arabic and Hindi sample rendering was visually checked. Audible reader checks were explicitly skipped; these programmatic checks do not establish a complete screen-reader user journey or mixed-direction editing certification.

## Local GM layout

The generated `gm-regions.json` reserves the right display for a trusted host's read-only GM inspector, with a player Console on the left and the Viewscreen in the centre. Local host inspection does not require a remote approval request. Remote GM clients still use the existing approval path. Clicking the inspector transfers keyboard focus to its tool panels; clicking a Console or focusing a player control through UIA returns it to the player surface.

A 600-frame native capture (`build/bridge-smoke/gm-final.json.png`) was inspected: the left Console radar and controls, centre Viewscreen, and right read-only inspector with all 101 entities rendered together. The inspector displayed the live committed tick and map. This checks the layout and trusted local data path; physical input checks remain as described above.
