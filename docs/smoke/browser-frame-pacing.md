# Browser frame pacing — 20 October 2026

Manually triggered investigation of a reported approximately 1 FPS in Chrome on desktop and mobile, beginning in the hosted lobby and continuing in play. That low frame rate was **not reproduced** in the available in-app Chromium browser.

The hosted site reported revision `74d0a5289f48eedd927cfafc7e2560266488272f`. Its navigation lobby loaded and accepted Ready. An isolated local copy of that deployment's server JS/WASM/data and packs, with timing around Emscripten's main-loop callback, measured 57–60 FPS and a median callback duration of 16.3 ms. It was served locally without connecting the diagnostic build to the external coordinator.

A local source build based on `eda7234`, before the pacing change, measured 59–60 FPS in the `training.navigation` / `training.player` / `training.survey` lobby. Representative samples spent 4–7 ms in drawing and 9–12 ms in `EndDrawing`. The available display/browser schedules animation frames at approximately 120 Hz; the canvas was 1280 × 720.

## Change and evidence

`open_window` previously set raylib's 60 FPS limiter on every platform. The WASM entry point separately registers an Emscripten animation-frame callback. raylib's `EndDrawing` waits to meet its own frame target; in this pinned Emscripten build, `nanosleep` calls `emscripten_thread_sleep`. This keeps the browser main thread occupied after the useful work is complete.

Web builds now use `SetTargetFPS(0)` and let the existing browser animation loop schedule presentation. Native pacing is unchanged. All three browser roles also support manually enabled `&perf=1` runtime diagnostics, with a five-second overlay and console report. Diagnostics are disabled by default.

After the change, a steady-state five-second lobby sample reported:

| Measurement                  | Result               |
| ---------------------------- | -------------------- |
| FPS                          | 120.03               |
| Frame interval p95 / maximum | 8.8 / 10.8 ms        |
| Mean poll and tick           | 0.014 ms             |
| Mean draw submission         | 4.107 ms             |
| Mean end-of-frame            | 0.012 ms             |
| Framebuffer / visibility     | 1280 × 720 / visible |

These are CPU wall times, not separate GPU measurements. Startup samples included asset-loading spikes and are not the steady-state result above. Local signalling was unavailable; the external service is not covered by these local measurements.

The local Ready/countdown transition also completed. The rendered navigation Viewscreen continued at approximately 120 FPS (frame p95 8.7 ms, mean poll/tick 0.118 ms, draw 0.768 ms, end-of-frame 0.006 ms). This was a sparse stationary scene, not a rendering stress test.

All three WASM application targets (`server`, `client`, `workshop`) compiled and linked. Changed C++ files passed formatting checks and the diff passed whitespace validation. The build reported an existing dangling-else warning in `settings.hpp`. No timing thresholds were added to automated tests.

The change removes a measured browser-main-thread stall, but does not establish that the reported 1 FPS issue is fully resolved. Retest the deployed change on the affected Chrome devices; if it persists, capture the opt-in overlay/console report with browser/device and scenario details. Native display performance, real mobile hardware and GPU timing were not measured here.
