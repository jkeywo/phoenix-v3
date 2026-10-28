# World and Console understandability refinement

Baseline: `615e2ff`. This follows the family migration and keeps schema 32, wire/content formats, tick phases, authority and worker/query lifetimes unchanged. The intent is to let a reader follow a workflow and its state without reconstructing one large class from unrelated files. Code growth is recorded without a quota.

## World

The former 25,157-line `world/bindings` folder is retired. Twelve private workflow chapters live under the [World family](../src/world/README.md). Each owns a source list and private partition of the existing simulation module; this is not twelve independent simulation libraries. Their own-folder global headers preserve MSVC JSON/Flecs reachability, and the Runtime/Clock identity and field order remain canonical.

Actor core now contains entity construction, capability indexing and generic native actuation. Fourteen scenario, display, command, sensor and waypoint methods moved to the appropriate workflow. Sequential `_01`/`_02`/`_03` file names were replaced with semantic names. Fixed order remains written explicitly in `schedule/schedule.cpp`; candidate restore remains in `persistence/checkpoint_bindings.cpp`.

Three RuleApplications forwarding methods were removed; callers name the actual owner operation. Eight repeated native envelope checks use the pure [command-shape policy](../src/control/command-shapes/README.md). Its data allows Controller/speaker fields and optional text byte limits; it grants no authority. All action, identity, generation, numeric, workload and physical checks remain in the native workflows. Coordinated request commands deliberately retain their separate protocol.

Processor recipe/status presentation now belongs to ResourceProcessor. World first filters permitted feeds, performs the existing ordered per-recipe store checks, and supplies selectable identities/manual eligibility. The owner formats the same recipe order, labels, mode, progress, work and revision without native lookup.

## Console

The [surface host](../src/ui/console-surface/README.md) retains Rml lifetime, scope, authored layout, render scheduling and event routing. The following owners are composed explicitly:

| Owner                                                            | Responsibility                                                                                                          |
| ---------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| [Document services](../src/ui/surface-host/README.md)            | Element/property/ObserverPtr markup caches and control placement/publication                                            |
| [Conversation panel](../src/ui/information-widgets/README.md)    | Browsing, mounted-view signatures, incoming text, acknowledgement, fitting and admitted actions                         |
| [Navigation/mission panel](../src/ui/navigation-panel/README.md) | Camera, selection, capture/pinch/cancel, keyboard/wheel, browsing snapshots, mission/navigation preparation and drawing |
| Choice descriptions                                              | Mode/direction labels, icons, artwork, aspect and option geometry, rendered by one implementation                       |
| [Rml shell asset](../content/ui/console/surface.rml)             | The unchanged built-in shell and styles, loaded before existing skin substitutions                                      |

Panel interfaces import shared presentation/input records and permitted ConsoleModel contracts, not Application or mutable sessions. Frames are synchronous borrows; panels retain their own state. Document caches preserve their original invalidation points, conversation text invalidation follows host cache reset, and pointer results explicitly tell the host when selection dirties a render.

The Rml asset is copied at configure time. Ninja link dependencies refresh WASM preload content; a Visual Studio asset utility updates the per-configuration native runtime copy on every surface build. Template edits do not depend on a C++ edit to be delivered.

Engineering/support/manual composition and authored layout still live in the surface host. World integrations still share private Runtime composition. These remain explicit coupling; the refactor does not hide them behind a universal context or claim a fully generic gameplay/UI engine.

## Audits and validation

The ordinary Runtime audit covers 816 prior out-of-line definitions: three forwarders retired, 804 remaining bodies retain their tokens after direct-owner call normalization, and nine intentional bodies implement the eight shared envelope checks plus processor projection delegation. Canonical Runtime declarations/inline helpers match after removal of the three declarations. The extracted Rml template matches its previous literal exactly. Independent production-caller review covered private partitions, envelope semantics, panel state/cache/visibility/focus/acknowledgement, navigation cancellation/browsing and data-defined camera geometry. Review caught an eager session dereference at the new navigation draw call; the caller now guards session availability.

The existing synthetic Away fixture adds malformed channel/request/argument/speaker cases through real World admission and asserts rejected envelopes leave authoritative state unchanged. The Comms smoke adds a Navigation journey in its already live Support session, using published page/control IDs and the existing pointer/keyboard probes before waypoint set/clear. Both changes are additive; all prior assertions and all other existing test/manual bodies are unchanged against `615e2ff`.

The Comms and camera smoke targets now declare the same explicit presentation/audio/raylib ordering as the production native executable. Building these previously excluded probes exposed Windows `CloseWindow`/`PlaySound` import collisions before that correction.

Executed checks:

- Full native Release build: `understanding-native-build-97.log`. All 48 CTest cases passed across `understanding-native-tests-102.log` and `understanding-native-rerun-108.log`. The main sandboxed run passed 44; three temporary-file cases and the synthetic loopback HTTP provider case passed on rerun with normal OS access. The initial sandbox failures are retained in the log.
- Fresh native build check and all 23 continuation traces exactly match migration starting revision `34883ac`: `understanding-native-traces-110.log` and `.json`. This is compatibility evidence for this refactor, not a new permanent content golden.
- All 12 local live-network cases passed without skips: `understanding-network-119.log`, including fleet recovery, live Console routing, content verification, GM approval and verified/untrusted WSS certificate behavior.
- Complete ordinary WASM build passed in `understanding-ordinary-build-113.log`; the final incremental source check passed in `understanding-ordinary-final-build-132.log`. The original runtime gate and native/WASM continuation comparisons all passed in `understanding-ordinary-runtime-133.log`, using the fresh native traces above. These are Node-hosted WASM/launcher checks, not a browser graphics journey.
- Complete threaded WASM build passed: `understanding-threaded-build-134.log`. The same runtime gate and native/WASM continuation comparisons all passed in `understanding-threaded-runtime-135.log`. The sequential ordinary-build/runtime → threaded-build/runtime pipeline exited successfully. Node-hosted WASM checks do not establish browser graphics or physical-device acceptance.
- Module ownership/dependency refusal fixtures pass with 682 C++ sources, 231 module units, zero exceptions and zero violations. UTF-8 literal audit retains all 47 non-ASCII occurrences without additions/removals. Runtime method and literal-template audits are described above.
- Native Comms graphical journey passed: `understanding-comms-smoke-114.log`. It exercised authored hails, incoming/choice/group messages, read semantics, shared display and narrow layout; captures live under `build/understanding-smoke/comms/`.
- The extended Comms/Navigation graphical journey passed: `understanding-comms-navigation-smoke-138.log`, after the probe build in `understanding-combined-probe-build-137.log`. It repeats every prior Comms assertion and additionally switches the published Navigation page, checks pointer/keyboard selection, then invokes visible waypoint set/clear controls through the real session path. The map capture is `build/understanding-smoke/comms/build/navigation-map.png`.
- Native camera graphical journey passed: `understanding-camera-smoke-116.log`. It exercised camera widgets, manual toggle, gamepad navigation, modal refusal and sixteen camera renders; captures live under `build/understanding-smoke/camera/`.
- Final pinned formatting check passed across 819 C/C++, 171 CMake, 11 PowerShell and 457 web/data/document files: `understanding-format-check-142.log`. The final receipt-only Markdown updates were formatted/checked separately. `understanding-source-audit-144.log` verifies all 682 manifest file hashes/counts, 296 local reading links, the literal Rml extraction and active provenance destinations. The production source fingerprint remains `35c5a36d0f333d07a93bdd751ea693a4e9e1590203ff9f389931e2f452f8d5f5`.

Representative Comms 1080p/narrow, camera/Console and Navigation map captures were inspected. Standalone navigation-preview attempts depended on stale direct-console/setup assumptions; they were not counted as passes. The verified journey uses the Comms probe's live Support session instead. The authored Courier fit label already contains mojibake in unchanged `content/base/definitions.json`; that separate content defect was visible in the Comms capture and is not evidence of a changed C++ string. Linux-native compilation, physical readers/devices, six-screen performance and public-network interoperability remain outside these local checks.

Graphical receipts use the local Windows native Release build on an NVIDIA GeForce RTX 5090 Laptop GPU, with a 1920×1080 display. Scripted actions/pointer/keyboard and the camera probe's gamepad-navigation adapter drive the journeys. Comms captures cover 1280×900, 1920×1080 and 600×900; the final Navigation capture is 1280×900 and the default camera journey is 1280×720. These runs establish no frame-time target or physical-device acceptance.

To repeat the two explicitly invoked graphical journeys from the repository root:

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_comms_smoke,phoenix_camera_smoke
./build/native/Release/phoenix_comms_smoke.exe ./build/native
./build/native/Release/phoenix_camera_smoke.exe ./build/native
```

For isolated capture directories, pass an absolute pack directory. The Comms probe writes below a `build/` child of its working directory; create that child first. The camera probe writes into its working directory. These probes remain excluded from automatic CTest execution.

## Size accounting

Use `python scripts/module-inventory.py --write --baseline 615e2ff` to refresh the [module table](module-sizes.md) and per-file hashes, then format the Markdown. Nonblank C++ counts include comments, imports and embedded text. The Rml shell is now presentation data and is reported separately; this move must not be mistaken for deleting its contents. Build scripts, Python tooling and reading guides are outside the C++ inventory.

| Reading size                                      |                        Before |                                After |
| ------------------------------------------------- | ----------------------------: | -----------------------------------: |
| All authored C++, nonblank                        |                       142,605 |                              143,170 |
| All authored C++, physical                        |                       145,547 |                              146,143 |
| Mixed World bindings folder                       |                        25,157 | Retired into twelve workflow folders |
| Actor core within those workflows                 | Mixed with domain integration |                                1,181 |
| Largest workflow folder, travel                   |        Part of mixed bindings |                                3,808 |
| Console surface host                              |                         7,785 |                                6,688 |
| Navigation panel                                  |               Part of surface |                                  667 |
| Information widgets, including conversation panel |                           118 |                                  536 |
| Shared document services and choice descriptions  |               Part of surface |                                  191 |

The net C++ increase is 565 nonblank lines (0.40%) and 596 physical lines. The extracted Rml shell contains 288 nonblank / 292 physical lines, now counted as presentation data. Counting that data alongside C++ gives a net increase of 853 nonblank / 888 physical lines. New private partition interfaces, standard/third-party include headers, explicit panel/frame contracts and folder-local composition account for the extra scaffolding. This is tracking information, not a size budget.

By family, the nonblank C++ changes are World +332, UI +179, Control +31 and Resources +23. Other families are unchanged.

The refreshed inventory contains 682 C++ files in 148 folders, with 216 primary named modules and 15 private partitions. The provenance ledger retains all 818 receipt rows and 342 nonempty original origins; four explicitly retired contextual-bridge receipts remain historical. All active destinations and 296 local reading-guide links resolve.
