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
