# Five-Console Rescue Cruiser frame rate

Windows Release, NVIDIA GeForce RTX 5090 Laptop GPU, 5760 × 2160 framebuffer, five claimed Consoles and one Viewscreen. The five-Console Rescue Cruiser run is the agreed workload. The separately authored Carrier exercise has two Consoles.

## Changes

The native frame spends its largest measured share in Console preparation. Console model refresh had been copying the full projected Controller array twice to find the default flight bindings. Flight input also copied that array every frame to compare Controller generations and ownership. Both paths now borrow those rows from their own Console observation. A model can reuse its reconciled state for the same immutable publication and selected tab, with the existing refresh handling tab changes and expiration of optimistic requests.

Authored layout lookup reuses the document's element cache and static component descriptions. Engineering preparation borrows permitted power and repair records while building panels. Helm skips building the general readout list it would immediately replace, and non-Engineering surfaces filter unowned system readouts in a single pass. This work remains in presentation and diagnostics. Command admission, evidence production and simulation scheduling use their existing paths.

## Reproduction

Build Windows Release and run the benchmark with fresh application storage for each invocation. Keep compilation and diagnostic profilers idle during the capture. The executable waits for all five active claims and then skips the first 120 frames. Use focus, adapter and framebuffer evidence to qualify each capture.

```powershell
$env:PHOENIX_PROFILE_PUBLICATION = '1'
build/native/Release/native.exe --local-only `
  --base build/current-cruiser-experiment/base.zip `
  --mission base.rescue-under-threat --slot courier --ship base.cruiser --console `
  --display-layout build/optimisation-2026-11-02/baseline/bridge-layout.json `
  --uncapped --performance-output build/frame-pass/bridge60-capture.json `
  --performance-seconds 120 --data build/frame-pass/bridge60-fresh-data
```

| Input                | SHA-256                                                            |
| -------------------- | ------------------------------------------------------------------ |
| Base pack            | `3484b3d15654e05430cd3f0609fd5cadf26f9d07aa5852cdfaec2143f5a11eaa` |
| Bridge layout        | `f3b1ed39f9f31b1fbc1c2ee50e9865893e7e77e7f963a47353b806f99a5aef40` |
| Baseline executable  | `8be2c609ab51fa5ae373b38a3fd7ef45e4985cab577152fc5bc78dee379aa16e` |
| Optimised executable | `7fb75d1dae6b0d2375c79e23905c2b8e59d0d7e0f8217b73822a56abb8b6dfcd` |

Frame durations are CPU wall time. The new `present-and-input-poll` stage measures the whole raylib `EndDrawing()` call, including its remaining submission work. Publication statistics include warm-up. Console data age measures ticks of simulation time behind the current tick.

## Matched results

Both executables were built from `d8a04161` with the same unrelated local audio, Viewscreen and application changes. The optimised executable adds the changes described above. All six captures ran for two minutes and retained focus throughout. Execution order was baseline, optimised, optimised, baseline, baseline, optimised. Raw reports, screenshots and logs are under `build/frame-pass/bridge60-match-*`.

| Pair and build |   FPS | Frame p50 | Frame p95 | Frame p99 | Ticks per second | End backlog | Console snapshots |
| -------------- | ----: | --------: | --------: | --------: | ---------------: | ----------: | ----------------: |
| 1 baseline     | 20.05 |  47.52 ms |  70.71 ms | 110.75 ms |             9.64 |     62.57 s |          44.98 ms |
| 1 optimised    | 65.92 |  10.65 ms |  32.15 ms |  35.80 ms |            10.06 |     59.72 s |          40.46 ms |
| 2 baseline     | 22.39 |  43.75 ms |  60.08 ms |  85.73 ms |            10.04 |     59.92 s |          39.88 ms |
| 2 optimised    | 65.83 |  10.52 ms |  32.58 ms |  36.27 ms |            10.05 |     59.89 s |          40.16 ms |
| 3 baseline     | 23.28 |  42.28 ms |  57.99 ms |  81.11 ms |            10.03 |     60.74 s |          39.31 ms |
| 3 optimised    | 64.76 |  10.72 ms |  33.32 ms |  36.40 ms |            10.01 |     60.64 s |          41.32 ms |

Median frame preparation went from 21.32–23.08 ms to 3.42–3.50 ms. Median Science projection went from 11.38–13.49 ms to 0.34–0.35 ms. Engineering layout still takes 23.58–24.83 ms on a frame that runs that stage. Median visible Console data age improved from 150 ms to 100 ms on every route. The optimised runs' longest frames were 334–568 ms. `EndDrawing()` took about 0.07 ms at p95. These captures do not isolate GPU execution or explain the worst outliers.

The render-rate target was met in all three runs. The simulation completed about ten ticks per wall-clock second and still accumulated about a minute of backlog during each two-minute run. This is a local virtual bridge workload with no sustained input or remote crew.

## Verification

Windows Release built successfully. The focused native knowledge, Controller, input and semantics, local-session and lobby-worker checks passed. The separate graphical widget smoke passed Details, command admission, compact layout and Workshop previews on its synthetic fixture. The ordinary WASM presentation library built, and its input/semantics and Controller integration executables passed under Node. The module ownership check reported zero violations.

The final executable also passed `--gui-navigation-smoke` on the five-Console Rescue Cruiser layout. The smoke waited for controls on all five routes and exercised controller traversal, numeric commit and cancel, Settings, text scale and input mode. Its log is `build/frame-pass/bridge60-five-console-input.log`.
