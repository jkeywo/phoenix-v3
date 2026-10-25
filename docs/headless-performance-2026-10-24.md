# Headless native runtime performance — 24 October 2026

The largest measured headless costs were repeated sensor eligibility calculations, full UI projections in frozen decision snapshots, deep copies of Controller evidence and repeated asteroid-cell generation. The fixes retain the same deterministic simulation results in the measured workloads.

## Measurements

Linux Release, Clang 19.1.7 (`-O3 -DNDEBUG`), AMD EPYC 9V74, four CPU equivalents and 16 GiB memory. Baseline simulation: `59c361e`; optimized simulation: `7d5334d`. Both use the same verified packs. These are local observations, not CI thresholds or production-content goldens.

Each cell below averages the corresponding statistic from two independent 600-tick runs. The first pair ran baseline then optimized; the second reversed that order. Compilation, integration tests, phase logging and CPU sampling were stopped for these measurements. Startup/content resolution and final saves are outside the timed region. Default authored profiles run autonomously, with no human claims or input.

| Workload       | Mean tick, before → after | p95 tick, before → after | Speedup | Mean reduction |
| -------------- | ------------------------- | ------------------------ | ------- | -------------- |
| Courier rescue | 30.12 → 13.61 ms          | 35.12 → 17.09 ms         | 2.21×   | 55%            |
| Cruiser rescue | 155.50 → 40.15 ms         | 198.22 → 66.97 ms        | 3.87×   | 74%            |
| Cruiser combat | 67.21 → 33.92 ms          | 112.88 → 58.45 ms        | 1.98×   | 50%            |

The [machine-readable receipt](headless-performance-2026-10-24.json) contains each run's mean, p50, p95, p99 and maximum, exact arguments, pack identities, final-state hashes and phase measurements. The reported percentile averages are not pooled percentiles. Raw logs, saves, preserved executables and CPU captures remain in ignored `build/headless-profile/`.

## What changed

- Sensor scans compute normal/outer range, mount eligibility, origin and heading once per scan. Candidate contacts still use the same distance, concealment, arc and region checks. This cache ends with the scan, so later power, damage, personnel, mode and position changes remain visible.
- Visibility checks for Comms disclosure and boarding eligibility use a contacts-only flavour of the same Console observation producer, preserving the exact feed/type/contact boundary while omitting combat and UI boards.
- Frozen decision observations retain permitted contacts, telemetry, combat facts and admission revisions. Processor choices, logistics/docking UI lists, sensor detail panels and reach geometry are built for presentation. A repeated compartment-permission scan is evaluated once per projection.
- Controllers borrow their frozen knowledge, inbox and combat arrays. Their decisions still read the pre-execution snapshot. Assisted craft share watch contacts only during the phase that queues orders; physical launch/movement follows afterward.
- Asteroid streaming reuses active-cell membership and generates only newly entered cells. Every pass still checks existing rock loading/unloading against current foci and motion. The derived cache keeps the existing active-cell bound and clears on restore; save schema and authoritative rock state are unchanged.
- The native scene-visibility header now includes `<iterator>` explicitly, fixing a Clang/Linux compilation failure found while building the smoke executable.

## Profiling evidence

The initial Cruiser rescue phase capture averaged 58.1 ms for Console observation snapshots, 24.4 ms for Controllers, 17.9 ms for craft/docking preparation and 15.3 ms for equipment. Snapshot construction represented approximately 38.5% of phase time. A 100 Hz userspace CPU capture, filtered to `World::tick`, attributed 37.6% of inclusive samples to `sensor_range` and 20.2% to personnel-property collection beneath repeated eligibility queries. Inclusive costs overlap and must not be added.

After those paths were reduced, asteroid streaming and JSON copy/destruction became prominent; the subsequent changes address both. A further CPU capture found full UI projections beneath Comms visibility checks, in a refresh phase averaging 8.9 ms per tick. Those consumers now use the contacts-only projection. Final phase measurements are recorded separately from the uninstrumented timing pairs. The earlier [native/browser graphical report](performance-2026-10-24.html) covers a different machine and workload and is preserved as historical evidence.

Instrumented Cruiser rescue phase means, measured separately from the timing pairs:

| Phase                         | Before   | After    |
| ----------------------------- | -------- | -------- |
| Console observation snapshots | 58.11 ms | 11.23 ms |
| Controllers                   | 24.39 ms | 6.07 ms  |
| Craft/docking preparation     | 17.94 ms | 0.89 ms  |
| Equipment                     | 15.27 ms | 10.68 ms |
| Comms/boarding/craft refresh  | 12.31 ms | 1.75 ms  |

## Reproduce

Build the explicitly invoked diagnostic and production packs:

```sh
./scripts/build-linux.sh phoenix_headless_profile test_missions_pack
mkdir -p build/headless-profile
build/linux/phoenix_headless_profile build/linux/base.zip \
  base.rescue-under-threat base.rescue-courier 600 build/headless-profile/courier
build/linux/phoenix_headless_profile build/linux/base.zip \
  base.rescue-under-threat base.delivery-cruiser 600 build/headless-profile/cruiser
build/linux/phoenix_headless_profile build/linux/base.zip \
  base.cruiser-combat base.cruiser 600 build/headless-profile/combat build/linux/test-missions.zip
```

The command selects the first authored ship slot and writes `<prefix>.json` plus `<prefix>.state.json`. Preserve the executable before editing to compare implementations against identical packs. Compare parsed final-state JSON for equality; keep those comparisons as manual evidence rather than checking shipped-content snapshots into CI.

For phase diagnosis, prefix a run with `PHOENIX_PROFILE_TICKS=1`. For optional Linux userspace CPU sampling, preload `libprofiler.so.0`, set `CPUPROFILE=<capture-path>` and inspect it using `google-pprof --text --cum --focus='World.*tick' <executable> <capture>`. This session used google-perftools 2.16 at its default 100 Hz. Omit both diagnostic modes when measuring ordinary tick timing.

## Validation and scope

All 41 native CTest integration cases passed. These include sensors and directed operations, personnel/modifiers, Controller coordination, combat/craft, logistics/docking, moving-rock collision/unload/revisit, save/restore and replication. All 12 uninstrumented measured runs produced complete final states equal to the preserved baseline for their workload. Production base and Test Missions packs verified during the build. All native integration executables and the Cruiser smoke executable compiled; compilation is not a claim that the full graphical Cruiser journey was run.

This measures direct headless `World::tick` CPU work. Session/network overhead, UI layout, rendering, GPU work, physical devices, Windows and WebAssembly were not measured here. Both Cruiser workloads still exceed the 20 Hz simulation's 50 ms budget at p95. These observations do not establish the six-display/60 Hz target.

The captured commands and raw measurements above retain the Cruiser fit ID from their archived content. Current content uses the normal `base.cruiser`; the separate delivery/logistics fit has been removed.
