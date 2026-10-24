# Native and web performance follow-up — 24 October 2026

Work in progress toward sustained 20 Hz simulation and 60 Hz presentation. The earlier [graphical report](performance-2026-10-24.html) and [Linux headless measurements](headless-performance-2026-10-24.md) remain historical evidence, not current acceptance.

## Fresh baseline and first presentation change

Windows laptop: Core Ultra 9 275HX, 24 logical processors, 64 GiB RAM. These captures selected Intel graphics, including Edge 156 ANGLE/D3D11, rather than the NVIDIA GPU selected by the earlier graphical investigation. Release native and WebAssembly builds include the landed sensor, decision-snapshot, Controller borrowing and asteroid-cell optimisations.

Content is frozen for comparisons at pack SHA-256 `0c49c05587061f5d50683608279fc3920441d91faf457b391cf5135763d05de4`. The current rescue fit is `base.logistics-cruiser`; `base.delivery-cruiser` is no longer present. Unrelated content edits continue in the shared checkout. The frozen pack and raw captures remain under ignored `build/performance-followup/`.

| Capture                                                      | Observation                                                                                       |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------- |
| Native Courier Helm, 180 frames                              | 14.31 FPS; mean tick 21.26 ms                                                                     |
| Native five-Console Cruiser + Viewscreen, actual 5760 × 2160 | Mean frame 2513.34 ms; Engineering preparation 1779.50 ms; mean tick 40.84 ms                     |
| Native autonomous Cruiser rescue, 600 ticks                  | Mean 39.67 ms; p95 45.59 ms; p99 109.93 ms                                                        |
| Corrected browser Cruiser Helm, 120 frames                   | Mean frame 365.57 ms; p95 447.90 ms                                                               |
| First fixed native bridge                                    | Mean frame 806.87 ms; Engineering preparation 29.62 ms, card generation 1.45 ms                   |
| First fixed browser Engineering                              | Mean frame 739.69 ms; preparation 46.98 ms, card generation 1.36 ms; projection remains 205.18 ms |

The Engineering change borrows the immutable Controller JSON and resolves each command's consumers once per card-generation pass. Previously the complete Controller array was copied repeatedly inside nested system/control loops. Card/control ordering and ownership remain unchanged. Separate timers show authored layout itself is approximately 0.27 ms in the browser Engineering capture; observation construction is now the larger cost. This is not a 60 FPS pass.

The baseline browser attempt exposed a build-staging defect: matching standalone ZIPs did not mean that WASM's `.data` embedded the same archive. The corrected baseline was relinked from unchanged compiled objects and its embedded pack independently hashed against native. The web build wrapper now stages changed verified archives before linking and updates their timestamps; unchanged archives are left alone. A manual profile pack override supports frozen-content comparisons without changing production content.

Captures run sequentially after compilation, with 20 warm-up frames and bounded samples. The first native multi-target build wrapper reported failure during reconfiguration; a focused profiling-target retry passed. Native and browser graphical profiling completed successfully. Further simulation, cache, interaction and full-gate validation follows with the remaining changes. Machine timings stay out of CI assertions.
