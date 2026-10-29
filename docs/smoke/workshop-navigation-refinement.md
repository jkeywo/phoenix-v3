# Workshop and Navigation verification — 29 October 2026

This receipt covers the implementation in the commit containing this file. The
production migration is limited to `content/base` and `content/test-missions`.
Independent development bases and historical snapshots are unchanged.

## Integration and content

- Native application and affected system-test targets build successfully.
- Native `system.content-and-packs`, `system.ftl`, `system.local-session`,
  `system.fleet-setup` and `system.network-content`: pass (five cases, 14.92 s).
- Production base and Test Missions verify through the real packager, including
  restricted Console companions. The base contains 25 entity templates and one
  reusable Courier ship template, `base.courier`; the Courier turret remains.
- WebAssembly server, client, Workshop, content check, FTL and transfer targets
  build. Content, FTL and transfer checks pass under bundled Node, executed from
  `build/web` so their preloaded data can be found.
- Fresh FTL continuation traces match between native and WebAssembly.

Synthetic cases exercise instance merge patches, distinct resolved definitions,
unchanged shared templates, invalid resolved content, restricted presentation,
dependency template choices, Workshop export/reopen, revision/cache reuse,
undo/redo, ranked preference persistence/reset and overlay validation/fallback.
The combined Navigation FTL case covers refused departure, prepare/cancel,
operator reassignment, two distinct instance definitions, save/resume and report.
These checks do not assert shipped machinery tuning.

WebAssembly application builds use the existing threaded configuration, optimized
C++ and development linking (`-O0`), rather than a fresh Release LTO gate.

## Explicit graphical journeys

`build/native/Release/phoenix_ftl_smoke.exe build/native` passes with production
base and Test Missions. It uses the actual Navigation tab and shared FTL widget,
checks panel interception against the map, switches battery preparation on/off,
waits for readiness, departs and checks rendered crew-report semantics.

Inspected captures in `build/`: `ftl-off.png`, `ftl-navigation-compact.png`,
`ftl-charging.png`, `ftl-ready.png` and `ftl-report.png`. Desktop is 1280 × 900;
compact is 390 × 844. The desktop map fills the page with a fixed FTL panel at
bottom-right. Compact uses the authored map-first column and a scrolling panel.
No objectives corner was added.

The browser Workshop was opened in Chrome through a build-only harness loading
the real `workshop.js` and preloaded production pack. The production renderer,
expanded equipment inspector and scrolling were inspected. The ordinary launcher
stalled in its fallback content verification after its sidecar size limit was
exceeded. Consequently this receipt does **not** establish the ordinary browser
launch journey or a browser Navigation/FTL playthrough. The harness is diagnostic
output in `build/web/workshop-graphical-check.html`, not shipped application code.

## Workshop performance

Machine: Intel Core Ultra 9 275HX, NVIDIA GeForce RTX 5090 Laptop GPU, driver
617.14, raylib/GLFW OpenGL 3.3, Windows, 1920 × 1080 display. Native Release,
1280 × 720 Workshop window, uncapped. Captures were sequential with no concurrent
build or browser activity. The production pack was loaded for each view.

Set `PHOENIX_WORKSHOP_PROFILE=build/workshop-final-<view>.csv` and
`PHOENIX_WORKSHOP_PROFILE_VIEW=entities|equipment|layout`, then run:

```powershell
build/native/Release/native.exe --base build/native/base.zip `
  --profile workshop-final-<view> --workshop-pack build/native/base.zip `
  --capture-frames 420 --uncapped
```

Discard the first 60 frames; each row below uses 360 remaining samples. All times
are milliseconds, lower-middle median and nearest-lower p95. Frame time includes
presentation; subsystem times are CPU wall time, not separate GPU measurements.

| View                                   | Frame median | Frame p95 | Worst frame | Workshop median | Workshop p95 |
| -------------------------------------- | -----------: | --------: | ----------: | --------------: | -----------: |
| Entity definitions                     |       1.1809 |    1.4427 |      5.4876 |          0.7247 |       0.8909 |
| Expanded equipment inspector           |       1.1389 |    1.4923 |      7.0286 |          0.7407 |       0.9456 |
| Layout palette and rendered Navigation |       3.0306 |    3.7291 |      9.0025 |          2.4812 |       3.0315 |

| CPU stage, median / p95 / worst | Entities              | Equipment              | Layout                   |
| ------------------------------- | --------------------- | ---------------------- | ------------------------ |
| Theme/cache lookup              | .0009 / .0013 / .0065 | .0008 / .0028 / .0083  | .0009 / .0030 / .0063    |
| Definition/Console lists        | .0220 / .0300 / .0370 | .0056 / .0110 / .3325  | .0029 / .0039 / .1724    |
| Map and verification            | .2824 / .4125 / .8169 | .2856 / .4623 / .7992  | .0006 / .0008 / .0072    |
| Inspector/equipment             | .3876 / .4829 / .6872 | .3928 / .5229 / 1.1636 | .0239 / .0379 / .0880    |
| Layout and material preview     | .0013 / .0015 / .0094 | .0012 / .0015 / .0017  | 2.4186 / 2.9738 / 5.1608 |
| Revision/history                | .0079 / .0099 / .0744 | .0077 / .0100 / .1984  | .0088 / .0115 / .2178    |

The layout palette's drawing is included in the layout stage. Unchanged scrolling
does not resolve equipment again, serialize the whole document for history or
compare resource bytes. Revision assertions separately verify cache reuse;
these captures do not isolate individual JSON calls. Screenshots and CSVs are
`build/workshop-final-{entities,equipment,layout}.*`.

A preserved pre-change binary and the current binary were also run on the same
original pack for a fixed 360-frame journey: 31,664.47 ms before versus
19,198.74 ms after. Files are `build/workshop-{before,after}*` and
`build/workshop-baseline.zip`. This is an end-to-end elapsed measurement including
startup, loading and default-view drawing. It is **not** a scrolling frame-time
comparison; the old binary has no per-stage CSV instrumentation. Before-change
scrolling percentiles remain unmeasured. No hardware timing threshold was added
to CI. All final measured scrolling frames fit the 16.67 ms target on this machine.

## Remaining acceptance

Physical screen-reader operation, touch/gamepad devices and the spanning
six-display journey remain unverified. Ordinary browser startup, the browser FTL
graphical journey and comparable pre-change scrolling percentiles also remain
outstanding. Builds, synthetic semantics and inspected native captures do not
substitute for those checks. Existing sessions follow ordinary content-identity
checks; no migration of old session content is supplied.
