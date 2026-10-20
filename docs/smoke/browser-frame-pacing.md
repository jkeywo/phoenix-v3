# Browser frame pacing — 20 October 2026

Manually triggered investigation of a reported approximately 1 FPS in Chrome on desktop and mobile, beginning in the hosted lobby and continuing in play. The initial Courier investigation did not reproduce it. The subsequent Cruiser investigation below reproduced the sustained slowdown and identified its cause.

The hosted site reported revision `74d0a5289f48eedd927cfafc7e2560266488272f`. Its navigation lobby loaded and accepted Ready. An isolated local copy of that deployment's server JS/WASM/data and packs, with timing around Emscripten's main-loop callback, measured 57–60 FPS and a median callback duration of 16.3 ms. It was served locally without connecting the diagnostic build to the external coordinator.

A local source build based on `eda7234`, before the pacing change, measured 59–60 FPS in the `test.navigation` / `test.player` / `base.courier` lobby. Representative samples spent 4–7 ms in drawing and 9–12 ms in `EndDrawing`. The available display/browser schedules animation frames at approximately 120 Hz; the canvas was 1280 × 720.

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

## Follow-up: Cruiser lobby compilation

After explicit permission to create a test session on the hosted site, Free Flight with `base.cruiser` was opened at `https://jkeywo.github.io/phoenix-v3/`. The live deployment had advanced to `8b50f454a5b2281edd533d70f981d64446283bcb`; it did not include the pacing fix. Its Cruiser lobby rendered, but interaction attempts repeatedly timed out. It was inspected in the browser's portrait viewport and at 1280 × 720. No runtime warnings/errors explained the delay. Launch input could not be confirmed on that slow deployed session, and no precise live-site FPS is claimed.

The **same Cruiser setup** in the local diagnostic build, already containing the pacing fix, measured 1.64–1.66 FPS at 1280 × 720. This confirms that removing the frame limiter alone was insufficient. A representative five-second sample reported 565.36 ms mean drawing, 319.86 ms preparation, 81.70 ms semantics, and 40.13 ms polling per frame.

`GameLobby::snapshot()` compiled the full authored controller layout on every call. Each projection request repeated machinery/resource/observation/tree validation; presentation and networking requested several projections per frame. The much smaller Courier concealed the cost. Polling continues to request lobby configuration after launch, so the redundant work was not confined to the visible lobby.

The lobby now retains compiled layouts by ship definition for the current content. Both projection and profile-choice validation use them. Add, replacement, clear, received content and restore invalidate the retained layouts. Changing readiness or the selected profile still generates fresh state and does not freeze projections.

With only that cache change added to the diagnostic build, the Cruiser lobby measured 97.8–98.9 FPS in representative five-second windows. Mean drawing fell to approximately 9.5–9.6 ms, preparation to 3.1 ms, semantics to 2.0 ms and polling to 0.02 ms. The Consoles/Crew tabs responded, and the local Ready/countdown transition reached the running Cruiser Viewscreen. Launch still had a one-time setup/loading stall; this change addresses the sustained per-frame cost, not startup latency.

Validation: all three WASM application targets rebuilt; the native synthetic lobby integration case passed against a private signalling fixture (42.6 seconds, no skips). The regression extension reloads the same ship ID with changed profile choices/default, checks the fresh projection, changes the selected profile, and rejects the removed profile. Existing admission, readiness, deterministic AI slots and launch checks also passed. Changed C++ formatting and diff whitespace checks passed. No production ship tuning or wall-clock performance threshold was added to CI.

These fixes remain local until published; the observed deployed session was not modified. Actual desktop/mobile Chrome retesting after deployment remains necessary.

## Follow-up: sustained Cruiser flight

Longer observation after the lobby fix exposed a second cost. The stationary Cruiser Viewscreen settled at 16.3 FPS as telemetry history filled: approximately 57.6 ms drawing, including 18.9 ms in the semantics/public-board stage, and 3.3 ms polling/ticking. Closing the deployed test tab did not remove that cost.

`GameplayKnowledge::public_board()` built each Console's full JSON projection before selecting shared records and public messages. This unnecessarily copied and serialized private telemetry histories and notifications on every public-board request. Public-board projection now omits those private sections and skips private message serialization. Normal Console projection, shared camera selection, message deduplication, public read-only flags and history bounds retain their existing behavior.

Existing synthetic gameplay knowledge, Controller runtime and camera integration executables passed, including publication privacy, public-message deduplication/actionability, unclaimed Viewscreen access, restore and camera projection checks. All three WASM role targets built successfully (only the server required relinking for this simulation-only change).

The rebuilt Cruiser completed Ready/countdown and remained at approximately 120 FPS through tick 1161 (about 58 seconds of simulation). The final five-second sample had 10.6 ms p95 frame interval, 0.98 ms mean drawing, 0.23 ms semantics/public-board work and 1.59 ms polling/ticking. This is the same stationary, empty Free Flight Viewscreen at 1280 × 720; it does not establish combat or occupied-console performance. The one-time launch stall remains separate from these steady-state results.
