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

## Courier Patrol investigation — 19 October 2026

A user reported about 2 fps after launching Courier Patrol with a Viewscreen and two native Consoles. The earlier synthetic bridge result above does not establish performance for this authored scenario. A new local-only capture on the same reported NVIDIA RTX 5090 Laptop GPU, spanning 5760×1200, reproduced a substantial slowdown but did **not** reproduce the exact 2 fps report.

The starting build was `4cc828e`. With 120 warm-up frames and 60 measured frames, Courier Patrol had a frame p50 of 80.75 ms and p95 of 84.33 ms. Console preparation dominated. Availability checks copied the complete knowledge state for each publication/message candidate, and message admission searched copied permitted evidence even for authorised newly authored intents. Those paths now validate without mutating or copying the complete state; actual commands retain their transactional copy. Console preparation retains unchanged panel placement/text and caches element lookups until structural markup changes. Per-Console projection, readout and layout timings are included in the manual capture.

The final 300-frame capture, after 120 warm-up frames, measured frame p50 **29.29 ms**, p95 **36.71 ms**, p99 **38.86 ms**, maximum **41.67 ms**. All 300 frames were focused with both Consoles active. This is approximately 34 fps at the median, versus approximately 12 fps in the starting capture. It is an improvement, **not a 60 Hz acceptance pass or confirmation that the user's exact 2 fps path is resolved**. Console layout remains material (Helm p50 10.79 ms, Engineering 4.22 ms), and scene submission occasionally stalls. These are CPU wall-clock timings, not isolated GPU timings. The final framebuffer was visually inspected.

Lobby screen assignment now applies the same default uncapped pacing as command-line spanning, while retaining explicit `--frame-limit` and `--vsync` choices and restoring single-window pacing when assignments are removed. The rendered `--lobby-regions-smoke` journey passed with independent Console routing, Ready/Back, heading/throttle, save and quit behavior. This does not add physical touch/controller or screen-reader acceptance.

Raw ignored artifacts: `build/bridge-smoke/patrol-current.json`, `patrol-detail.json`, `patrol-precheck.json`, `patrol-fixed.json`, `patrol-final.json`, `patrol-lookup.json` and corresponding logs/PNGs. The comparison uses the same scenario, ship, display configuration and uncapped local-only command; sample counts and elapsed simulation time differ. Reproduce the final capture with:

```powershell
build/native/Release/native.exe --local-only --span-displays `
  --game-pack build/native/base.zip --mission base.courier-patrol `
  --slot base.player --ship base.courier-engineering `
  --performance-output build/bridge-smoke/patrol-lookup.json --performance-frames 300
```

The synthetic knowledge integration case additionally checks that repeated Console availability projections leave saved state unchanged. Native system checks and the UI smoke are separate from these authored-content performance measurements. Web performance and the ordinary networked Courier Patrol launch were not captured in this investigation.
