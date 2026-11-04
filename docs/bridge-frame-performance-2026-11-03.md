# Rescue Cruiser bridge frame performance

Windows Release, NVIDIA GeForce RTX 5090 Laptop GPU, 5760 × 2160 framebuffer, five claimed Consoles and the Viewscreen. Both builds use the same working-tree sources apart from the Console optimisation patch. Both use the same archived Rescue Cruiser pack, SHA-256 `3484b3d15654e05430cd3f0609fd5cadf26f9d07aa5852cdfaec2143f5a11eaa`, and virtual bridge layout. Each run uses fresh application storage, 120 warm-up frames and a two-minute uncapped capture. All measured frames stayed focused.

| Measurement                                      |  Baseline | Optimised |
| ------------------------------------------------ | --------: | --------: |
| Average FPS                                      |     13.87 |     19.73 |
| Frame time p95                                   | 113.09 ms |  71.16 ms |
| Frame time p99                                   | 147.80 ms |  95.65 ms |
| Median frame preparation                         |  41.40 ms |  24.67 ms |
| Median Console drawing                           |   6.38 ms |   6.97 ms |
| Median Engineering layout refresh                |  62.42 ms |  29.49 ms |
| Median Engineering station-tab preparation       |  10.06 ms |   0.70 ms |
| Median visible Console data age, all five routes |    150 ms |    100 ms |
| Simulation ticks during capture                  |       677 |       741 |

Console preparation reuses the responsive layout structure used to list station tabs. Session and content changes invalidate it. The active definition, current permitted availability, viewport, scale and page selection determine reuse. Labels, attention indicators and semantic actions still come from the current Console state.

Widget preparation groups the current permitted control rows by their projected Controller owner, builds models when a visible part needs them, and borrows Controller metadata and power limits from the supplied immutable observation. Control order, operation and channel checks, generation and command admission remain in their existing paths. This work occurs within each Console surface and uses the existing granted views. No new evidence producer or information-sharing route is introduced.

The frame timings are CPU wall time and include drawing and presentation. GPU execution was not timed separately. Layout and tab stage timings are inclusive and should not be added. The optimised run ends with 83.22 seconds of simulation backlog and achieves about 6.17 simulation ticks per wall-clock second. Engineering and Science remain the largest measured Console preparation costs. This workload has no sustained input or remote crew, and its regions do not establish physical six-display acceptance.

Raw captures, executable/pack/layout hashes, screenshots and build logs are under `build/frame-pass/`. The matched pair is `native-frame-matched-baseline` and `native-frame-matched-after`. The early `before` capture lost focus and is excluded from the comparison.

Native Controller runtime, gameplay knowledge, input and semantics, and lobby worker Console checks passed. The separate shared-widget graphical smoke exercised readouts, Details, admission, compact layout and Workshop previews.

## Worker publication follow-up

`PHOENIX_PROFILE_PUBLICATION=1` adds the simulation worker's publication count and mean stage times to the frame capture. A diagnostic run overlapping compilation averaged about 160 ms per publication, including 157 ms for Console snapshots and 0.24 ms for the scene. A temporary breakdown over 768 Console projections attributed about 80% of their combined construction time to Controller rows. These are diagnostic samples from a loaded machine.

The worker now decodes the permitted observation once per Console projection and supplies that read-only input to each Controller's availability check. Recipient restriction and inspection evidence still run separately for each Controller. The scope ends with the projection. The same pass avoids reconstructing the already assembled FTL board and copying completed Controller JSON subtrees. The presentation side reuses authored Console specifications and visits projected command consumers once to derive bindings and power availability.

The native Controller runtime, gameplay knowledge and local-session checks passed, as did a repeat of the graphical widget smoke. The complete saved World after 200 fixed Rescue Cruiser ticks matched the earlier capture on the same archive byte for byte. The save SHA-256 was `16af2e6e9276c677ea9196b50ddcdce6894b6e96bd90e414c4fc844166dd301e`.

The second pair isolates the worker changes. Both builds include the updated Console fitting lookups and availability traversal. Both runs use the same pack, five Consoles, Viewscreen, 5760 × 2160 framebuffer and NVIDIA adapter as the first pair. These captures use fresh application storage, 120 warm-up frames and one minute of sampling. All measured frames stayed focused. Worker publication means include preparation and warm-up.

| Measurement                                        | Before worker change | After worker change |
| -------------------------------------------------- | -------------------: | ------------------: |
| Mean Console snapshot construction per publication |            122.33 ms |            52.54 ms |
| Mean worker publication                            |            123.86 ms |            53.98 ms |
| Simulation ticks during capture                    |                  350 |                 601 |
| Simulation ticks per wall-clock second             |                 5.83 |               10.01 |
| Average FPS                                        |                21.52 |               21.18 |
| Frame time p95                                     |             62.75 ms |            62.23 ms |
| Frame time p99                                     |            101.27 ms |           110.28 ms |
| Median visible Console data age in simulation time |               100 ms |              150 ms |

The worker spends 57% less time constructing Console snapshots per publication and completes 72% more ticks in the capture. Average FPS stays near 21 while p99 worsens by about 9 ms. The host still has 32.10 seconds of simulation backlog at the end. The second pair's raw reports are `build/frame-pass/projection-pair-before.json` and `build/frame-pass/projection-pair-after.json`.

The ordinary WASM Console surface and Controller integration executable built successfully. The Controller suite passed through its Node runner. The module ownership check reported zero violations.

## Remaining Console snapshot work

A separate diagnostic timed 1,024 Console projections after the worker change. About 40% of their combined time went into constructing Controller rows, including action availability probes. Physical and knowledge capture accounted for about 25%, command descriptors for 13%, and the rest of Controller projection for 11%. Other boards accounted for the remainder. The instrumented run lost focus and serves only as a breakdown of the work.

The Controller, objective diagnostic and knowledge record encoders now construct JSON objects in place and reserve known output array sizes. Recipient restriction builds its result directly from permitted records and deliveries and carries only the decoded payloads associated with retained records. The same recipient checks, evidence fields, sequence order, action availability evaluation and command admission apply.

One-minute reruns used the same frozen pack, RTX 5090 Laptop GPU, five Consoles, Viewscreen and 5760 × 2160 framebuffer. The previous executable averaged 60.66 ms per snapshot batch and the updated diagnostic executable averaged 44.67 ms. Worker tick means were 57.16 and 57.31 ms. The previous executable was focused for 339 of 615 frames and the updated executable was unfocused throughout this pair. MSBuild also reused the temporary profiling objects in the first updated executable. These runs serve as diagnostic evidence. Their raw receipts are `residual-before.json` and `residual-after.json` under `build/frame-pass/`.

The instrumented Controller row construction stage, including action availability probes, fell from roughly 3.73 ms to 1.67 ms per Console projection. The restored World source files were forced to recompile for the clean capture.

The clean one-minute capture kept focus for all 1,212 frames. With all five Consoles active, mean snapshot construction was **42.44 ms** per publication, down 19% from the previous fully focused 52.54 ms capture. Mean worker publication was 43.65 ms. The clean run averaged 20.17 FPS, with a 71.72 ms p95 frame time and 594 simulation ticks. The earlier focused run averaged 21.18 FPS and completed 601 ticks. These captures show cheaper snapshots and no measured graphical or simulation throughput gain across the two sessions. The clean receipt is `build/frame-pass/residual-clean.json`.

The native knowledge, Controller runtime and local-session integration checks passed. The ordinary WASM Controller suite passed through its Node runner. The module ownership check reported zero violations.
