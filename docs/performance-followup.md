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

## Shared snapshots and derived evidence

Immutable permitted observations are now shared among presentation consumers, with invalidation at simulation ticks, commands, claims, releases and provider changes. Synthetic tests cover sharing, same-tick command invalidation, retained-reader immutability and route reuse without information leakage. Native and WebAssembly snapshot checks passed before the subsequent evidence changes.

Action availability now reuses one observation per Controller and one physical/knowledge projection per Console. Sensor evidence borrows permitted knowledge records and caches decoding by record identity and exact payload; personnel property queries collect only the requested targets. Readout Controller lookups borrow arrays. Native knowledge, sensors, personnel, Controller and session checks pass. A 600-tick frozen-pack continuation is byte-for-byte equivalent as parsed JSON to the fresh baseline.

| Intermediate capture | Result |
| --- | --- |
| Shared-snapshot native bridge | 676.30 ms mean frame; Console rendering stage 23.08 ms |
| Evidence/target filtering native bridge | 402.79 ms mean frame; 34.33 ms mean tick |
| Shared availability projection native bridge | 332.36 ms mean frame; 33.43 ms mean tick; Engineering projection 9.26 ms |
| Current autonomous Cruiser, 600 ticks | 34.96 ms mean; p95 53.18 ms; p99 109.74 ms |

These captures do not pass 60 FPS or sustained 20 Hz graphical simulation. The profiler currently clamps elapsed simulation time at 250 ms per frame, so `captured_ticks / capture_seconds` must also be inspected; slow frames can discard wall time. The final acceptance harness must report discarded time and backlog explicitly.

The next browser build exposed missing local Emscripten compiler/linker binaries after the previous successful runs. Repair of the pinned 6.0.12 SDK is in progress. Subsequent evidence changes still require WebAssembly validation. The browser semantic DOM update now indexes existing controls once per pass and reads canvas placement once, preserving action order and focus handlers.

## Incremental history and avoidable layout work

Knowledge history eviction now maintains a set of superseded identities per board instead of rebuilding all record keys on every write. It keeps the same latest-per-key protection, historical limit and chronological order; restoration rebuilds this derived index. Synthetic tests with a deliberately small history limit cover interleaved keys, eviction, refusal of delayed evidence and exact save/resume continuation. Gameplay, sensors and Controller integration checks pass on native and WebAssembly.

Console visibility filtering now rejects other routes before querying the document, then uses the existing element lookup cache. The native bridge capture falls from 332.36 ms to 292.74 ms mean frame; preparation falls from 130.98 ms to 90.12 ms. Additional preparation timers identify RmlUi updates as a remaining cost. Unchanged sensor titles, objective labels and instrument text now avoid reparsing identical leaf markup. Observer pointers prevent cached markup from being reused after an element is destroyed.

The profiler now reports discarded simulation seconds, maximum backlog and remaining backlog. The browser runner supports headed captures and disabling CPU sampling, derives its timeout from the capture duration, retains completed measurements on capture timeout and closes its own browser in a finally block.

The pinned SDK was repaired using CRC-checked entries from its 6.0.12 release archive, reusing the already installed identical Clang binary and local Node runtime. Full WebAssembly integration execution resumed successfully. Performance acceptance remains pending.

## Indexed equipment and shared read-pass telemetry

Fitted equipment is now addressed by a derived name-to-entity index; its ordered handle vector remains the source for deterministic execution. Duty-to-equipment relations cache only resolved immutable definition relationships. Sensor evidence chooses the same first maximum timestamp before decoding ordinary capabilities, retaining local evidence on ties; finding bundles retain their original per-entry merge behavior.

A fixed decision-snapshot pass shares physical telemetry among overlapping Console feeds. Presentation readers share a separate telemetry cache, cleared with permitted snapshots on every existing tick/command/claim/provider invalidation boundary. Neither cache is serialized or reused across mutable simulation phases. Native and WebAssembly personnel, sensors, Controller, simulation and session checks pass.

The frozen native autonomous Cruiser is now 22.60 ms mean tick, p95 26.97 ms, p99 30.76 ms (600 ticks). The intermediate native bridge is 221.07 ms mean frame. Its readout scroll/semantic probe passes. This remains far from the presentation target.

A buffered-geometry renderer was trialled on native and WebGL2 and rendered the sampled Console correctly, but did not materially reduce the Console stage. It has been removed; its patch and diagnostic captures remain under ignored performance artifacts. The original renderer is retained. Separate timers now distinguish RmlUi view updates, custom painting and RmlUi rendering.

## Presentation and accessibility checkpoint

Console models retain resolved Console definitions until the session/catalog changes. Windows accessibility publication now indexes current identities once, moves the previous immutable control list instead of copying it, and indexes prior controls for property notifications. The native COM smoke passes stable identity across reorder/name changes, retirement/recreation, generation replacement and text-role checks.

Resource-card text now uses the same offscreen deferral as generic readouts, while publishing current accessible values. A graphical probe verifies a stale offscreen value is already current semantically and refreshes after scrolling. It passes on native and WebAssembly. The bounded HarfBuzz width cache includes face, exact text, language, direction, kerning, letter spacing and preceding character. It clears on font loading, language registration, resource release and shutdown. Native and browser smoke modes each compare at least 100 cache hits directly against HarfBuzz without mismatch. The probe is disabled in performance samples.

The current native bridge sample (five Consoles plus Viewscreen, 5760 � 2160) is 177.52 ms mean frame, with 63.98 ms preparation, 17.80 ms Console rendering and 24.10 ms mean tick. It remains a failed 60 FPS result; about 404 ticks execute during 20.30 seconds, with 0.054 seconds of elapsed simulation time discarded by the diagnostic cap. The font/accessibility functional sample is separate and is not performance evidence.
