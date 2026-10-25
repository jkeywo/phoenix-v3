# Native and web performance follow-up — 24 October 2026

Work in progress toward sustained 20 Hz simulation and 60 Hz presentation. The earlier [graphical report](performance-2026-10-24.html) and [Linux headless measurements](headless-performance-2026-10-24.md) remain historical evidence, not current acceptance.

## Fresh baseline and first presentation change

Windows laptop: Core Ultra 9 275HX, 24 logical processors, 64 GiB RAM. These captures selected Intel graphics, including Edge 156 ANGLE/D3D11, rather than the NVIDIA GPU selected by the earlier graphical investigation. Release native and WebAssembly builds include the landed sensor, decision-snapshot, Controller borrowing and asteroid-cell optimisations.

Content is frozen for comparisons at pack SHA-256 `0c49c05587061f5d50683608279fc3920441d91faf457b391cf5135763d05de4`. The current playable Cruiser is `base.cruiser`; its former separate logistics fit has been removed. Frozen captures retain their original ship identifiers. Unrelated content edits continue in the shared checkout. The frozen pack and raw captures remain under ignored `build/performance-followup/`.

| Capture | Observation |

| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------- |

| Native Courier Helm, 180 frames | 14.31 FPS; mean tick 21.26 ms |

| Native five-Console Cruiser + Viewscreen, actual 5760 × 2160 | Mean frame 2513.34 ms; Engineering preparation 1779.50 ms; mean tick 40.84 ms |

| Native autonomous Cruiser rescue, 600 ticks | Mean 39.67 ms; p95 45.59 ms; p99 109.93 ms |

| Corrected browser Cruiser Helm, 120 frames | Mean frame 365.57 ms; p95 447.90 ms |

| First fixed native bridge | Mean frame 806.87 ms; Engineering preparation 29.62 ms, card generation 1.45 ms |

| First fixed browser Engineering | Mean frame 739.69 ms; preparation 46.98 ms, card generation 1.36 ms; projection remains 205.18 ms |

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

The current native bridge sample (five Consoles plus Viewscreen, 5760 × 2160) is 177.52 ms mean frame, with 63.98 ms preparation, 17.80 ms Console rendering and 24.10 ms mean tick. It remains a failed 60 FPS result; about 404 ticks execute during 20.30 seconds, with 0.054 seconds of elapsed simulation time discarded by the diagnostic cap. The font/accessibility functional sample is separate and is not performance evidence.

## Physical sensor read-pass reuse

Read-only snapshot passes now share the raw physical returns of the same ship/sensor. Each Console still merges those returns through its own feed selection and retained evidence, including its own identity and finding permissions. The cache is confined to the decision pass or cleared with the presentation epoch; actuator and mutable sensor phases retain independent queries.

Native and WebAssembly simulation and focused sensor checks pass. The 600-tick frozen rescue continuation still matches the fresh baseline exactly. Mean native tick is 21.76 ms, p95 26.39 ms, p99 29.51 ms. This remains above a 16.67 ms frame budget even before rendering, so further frame-loop work is required for the requested presentation target.

## Historical frame-loop prototype (not acceptance)

An explicit native `background` diagnostic now owns the authoritative SessionPort on a worker. Its fixed 50 ms clock never discards simulation time. Rendering reads immutable permitted Console snapshots at nominal 10 Hz and scene snapshots after every simulation tick. Eight retired frames are retained on the worker to keep ordinary snapshot destruction off the rendering thread. Commands and lease changes still return their authoritative result synchronously; this path needs sustained-input measurements before production activation.

Console preparation reuses identical snapshots and spreads fresh models across frames. Cached RmlUi layers are repainted within the frame budget; authority/scope changes, input and changed interactive bounds force immediate updates. Accessibility metadata retains Console order and current prepared values. Measurements include data age, pending-paint age and visible-data age. These are latency costs: a high render rate alone does not establish interactive acceptance.

The worker's frozen Cruiser continuation matches synchronous execution exactly at 420 and 424 ticks; the real release/reclaim path passes. A synthetic production-loader session check also verifies fixed-tick continuation, retained-reader immutability, admission retirement, route reuse and generation reset. No wall-time performance threshold was added to CI.

Native intermediate captures cross 60 FPS on average, but still fail the p95/p99 frame-time limits. `zero-copy-native-prototype.json` records 15.22 ms mean, 20.87 ms p95 and 28.18 ms p99 on Intel graphics. This is a short diagnostic, not a two-minute acceptance run. Scene visibility preparation contained a redundant synchronous framebuffer query; immutable scene delivery now avoids copying scene vectors in the rendering thread. Console readout builders borrow immutable JSON subtrees instead of repeatedly copying them. Shader uniform locations cache for the owning renderer lifetime and clear when its shaders are released.

The laptop also has an RTX 5090. GLFW's standard Windows hybrid-GPU preference is available through `PHOENIX_PREFER_HIGH_PERFORMANCE_GPU` (opt-in ON; default OFF while capture results are evaluated). Driver logs confirm the NVIDIA adapter in the separate `gpu-preference-native` and `uniform-cache-native` captures. Those results still fail the tail target and are not directly attributed to code speedup over the Intel baseline. The buffered-geometry trial was repeated after CPU optimizations; it remained slower and was removed again.

This checkpoint predates activation of the production worker paths. See the pause checkpoint below for their current state and the remaining acceptance work.

## Pause checkpoint — 25 October 2026

Work is paused at the user’s request after completing the native WebAssembly exception optimization and its focused validation. This is a progress checkpoint, not final acceptance of the complete revised plan.

Native production entrypoint captures with the frozen Cruiser, five Consoles and Viewscreen at 5760 × 2160 (`production-native-final-1/2/3`) average 79.18, 78.82 and 78.26 FPS over 120 seconds each. Frame p95 is 17.72, 17.97 and 18.15 ms; p99 is 21.27, 21.52 and 21.73 ms. Each capture advances exactly 2400 simulation ticks, with approximately 0.02 seconds of final worker backlog. These runs use the offline session path and predate the final unchanged-semantic-publication shortcut. One p95 narrowly misses the chosen 18 ms threshold. They exercise six logical regions on the laptop framebuffer, not six physical displays.

The pre-exception-change web Engineering diagnostic (`current-manual-web`) averages 77.81 FPS over 20 seconds, with 401 simulation ticks and no remaining backlog. Its frame p95 is 14.61 ms and p99 15.77 ms. The real networked browser host exposed additional problems absent from that diagnostic: large hashing scratch allocations, a fresh angled-turret save/restore bug, duplicate world construction during online launch, and expensive JavaScript exception trampolines. Hashing is now bounded without changing identities, angled turrets initialize correctly, and online startup avoids the redundant world.

The isolated worker build now uses `-fwasm-exceptions` with matching WebAssembly `setjmp`/`longjmp` support across C and C++ dependencies. The ordinary build retains JavaScript exception support. Browser feature selection checks isolation, shared memory and WebAssembly exception support before selecting worker assets. Native and WASM input/session integration pass, including queued command admission, retained immutable readers and portable paused recovery. Native combat and content cases, the real synthetic native lobby/actor case, graphical readout/COM checks, and 31 launcher checks also pass. Static-host service-worker isolation and blocked-service-worker fallback selection were exercised locally before the exception capability check was added. A complete rebuilt CI/deployment matrix has not been run or published.

The first real host capture after the exception change (`production-web-native-eh-short`) reports 58.18 FPS, p95 18.05 ms and p99 18.90 ms over 30 seconds. Main-thread draw work falls to 2.04 ms mean and worker commit work to 18.68 ms mean. The 60 Hz timeout schedule is retained: a subsequent animation-frame trial (`production-web-native-eh-raf-short`) regressed to 52.81 FPS despite 1.97 ms mean draw work, and was reverted. The capture includes startup catch-up (659 ticks), so it is not steady-state 20 Hz acceptance. The WebAssembly heap reaches its 2 GiB capacity and remains a measured concern for further startup/player/memory validation.

Remaining after this pause: bind the worker clock’s startup/quorum pause boundary to actual simulation authority; complete clean long captures on the final native/browser entrypoints and the real player Console path; exercise current headed browser interaction and memory behaviour; and finish verification of the ordinary/worker deployment matrix. Existing raw measurements, failed trials and logs remain in the ignored `build/performance-followup/` directory. No physical screen-reader, touchscreen or six-monitor acceptance is claimed.
