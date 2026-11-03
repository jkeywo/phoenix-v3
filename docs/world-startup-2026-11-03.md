# World construction and catalogue resolution, 3 November 2026

World now reuses the Controller actions, candidates and objective bindings retained by catalogue preparation. It still binds fitted endpoints and creates separate Controller state per instance. Entity assembly also shares one sprite compiler for the unchanged catalogue, so sprites using the same asset reference reuse verified dimensions. Entity order, knowledge scope, command authority and tick scheduling are unchanged.

Catalogue preparation now compiles the main Controller policy once. The action and objective restrictions inspect that program, and the candidate compiler runs once. This keeps the main program's source and saved signature, and preserves verification of authored executors, availability, objectives and candidate operations. Preparing an edited catalogue rebuilds the derived artifacts.

## Measurements

Native Release, `base.rescue-under-threat` with `base.cruiser`, same 120,819,995-byte archive (SHA-256 `9a83303afcd31f1d595ff5d77a75064edcfe5fa197b9eea59b57df547d6f341b`). The timing binaries use the same `a938bea8` base source. Each row is a new process with no application catalogue cache; the filesystem cache was not flushed. The run verifies the archive and constructs the authoritative World. Snapshot serialization happens after the timing. This is a local headless measurement; presentation, transport and client admission are covered by the separate [five-client receipt](cold-start-2026-11-02.md).

| Variant           | Run | Archive (s) | Catalogue (s) | World (s) | Total (s) |
| ----------------- | --: | ----------: | ------------: | --------: | --------: |
| Baseline          |   1 |        1.29 |         16.87 |     24.39 |     42.55 |
| Baseline          |   2 |        1.21 |         17.70 |     27.78 |     46.69 |
| Baseline          |   3 |        1.26 |         16.56 |     23.87 |     41.69 |
| World reuse       |   1 |        1.22 |         16.58 |     15.31 |     33.11 |
| World reuse       |   2 |        1.35 |         63.91 |     30.53 |     95.79 |
| World reuse       |   3 |        1.36 |         16.89 |     12.51 |     30.76 |
| World + catalogue |   1 |        1.26 |         14.65 |     12.88 |     28.79 |
| World + catalogue |   2 |        1.26 |         18.50 |     36.46 |     56.23 |
| World + catalogue |   3 |        2.49 |         16.24 |     13.21 |     31.95 |

Median World time was **24.39 → 15.31 s** at the World step and **13.21 s** with both changes. Median catalogue resolution was **16.87 → 16.24 s** across the full comparison (**16.89 → 16.24 s** from the World-only variant). Median summed time was **42.55 → 31.95 s**, about **25% lower**. The shared machine was variable; the slow runs remain in the table. The catalogue timing difference is small relative to that spread, though the duplicate compilation has been removed.

The baseline median for World definition preparation was 20.99 s; instance creation was about 0.15 s. With both changes, median definition preparation was 10.11 s. The largest remaining measured catalogue cost is cross-definition verification (11.05 s median in the final variant); scenario setup also remains visible in World (1.93 s median).

All nine initial World snapshots have the same SHA-256, `205ba73bf44d08cf0948f6e16ca89939f7859254775198d6a318366dd6d3e3c8`. Run logs, snapshots and frozen executables are under `build/world-startup-2026-11-03/`. Executable SHA-256 values:

- Baseline: `0d385afc6d84489bcf5af693c7fea0135fccb2db39de3ac6cb1073b01d37aca7`.
- World reuse: `9f57309e6a04c90a5ebe0369490af07b2bf2a7766771536de18eeeb1fef7bd52`.
- World and catalogue: `d6a4f2637b1156292e3007a1faceee273d134fb0769a8a5f2735f6a85d9e1ecd`.

To repeat, build `phoenix_headless_profile` in Release, use an existing verified pack archive (build `base_pack` separately if needed), and run with `PHOENIX_PROFILE_STARTUP=1`:

```text
phoenix_headless_profile.exe <base.zip> base.rescue-under-threat base.cruiser 0 <output-prefix>
```

The probe reports archive loading, catalogue resolution and World load times separately. The zero tick count selects a load-only run.

## Verification

On the integrated source (including the independent changes through `758825a1`), the four affected native tests passed in 52.21 seconds of test execution: `system.content-and-packs`, `system.motion-radar-trees`, `system.local-session` and `system.controller_runtime`. The module boundary check reported zero violations. The module inventory and graph were regenerated from that source.

Ordinary WASM Release built successfully and passed `phoenix_content_check` and `phoenix_controller_runtime_tests` under Node. This follow-up used focused checks; the long suite and five-client timing were not rerun.
