# Modularisation migration evidence

Work started 27 October 2026 at 23:12:41 UTC from `34883ac` with a clean working tree. The user requested the full [migration plan](modularisation-plan.md), targeting approximately ten hours while preferring correctness to a hard deadline. No push or deployment is authorised by this migration.

## Execution approach

Root coordinates source ownership, CMake integration, validation and commits. Parallel work is divided between content, UI/graphics, Workshop/application, World/control/knowledge, effects, and dependency enforcement. Shared World integration has one owner; compiler interfaces and state contracts are coordinated explicitly.

The initial relocation is one mechanical integration step across all existing sources. This deliberately precedes the numbered domain extractions to let parallel workers use stable final paths and to avoid repeating global path edits during interface work. Relocation is recorded separately from extraction: an included fragment in its destination folder remains unfinished and is not counted as a completed module.

Current compiler functions contain genuine strongly connected call groups (including effects/abilities/processes and combat/directed definitions). Narrow declaration and compiler interfaces will be split first; mutually recursive implementations may temporarily share an explicitly documented compilation target. This is not permission for interface-import cycles, duplicate declarations or permanently unrestricted dependencies.

## Chunk 00 — baseline

- Starting revision: `34883ac` (`Record obsolete effect cleanup verification`).
- Inventory: 342 first-party files under `src/`; destination ownership mapping is retained locally at `build/modularisation-baseline/source-map.json` before relocation.
- Native gate: passed all 46 system suites (593.11 seconds), the native build check, all 23 trace-producing journeys and all 12 live-network cases (251.90 seconds for the network group). Output is in `build/modularisation-baseline/native.log`; the 23 starting-revision traces are preserved in `build/modularisation-baseline/traces/`.
- Existing threaded WebAssembly baseline rebuild: explicitly interrupted before completion to unblock source extraction; output in `build/modularisation-baseline/web-build.log`. This is incomplete evidence, not a pass. Both browser build modes and cross-platform continuation remain integration gates.
- Initial sandboxed native build stalled after MSBuild's glob check and was interrupted without completing. It is not a test pass or a code failure. The normal-process/native-network run is recorded separately.
- Linux, graphical, device, screen-reader and public-network smoke checks have not run as part of this work.

No numbered extraction chunk is complete yet. Each subsequent receipt must state the actual implemented seam, checks executed and any remaining migration exceptions.

## First content checkpoint

The content family now has separate pack/codec, catalogue, definition, compiler, verification, distribution, UI, asset and document owners. Genuine compiler call cycles remain grouped in the explicit static compiler target; their named interfaces are acyclic. The new `content/distribution` module keeps public client/launcher packaging above assets and verification rather than introducing a reverse asset dependency.

- Native `phoenix_content` build passed; log: `build/modularisation-baseline/content-agent-03.log`.
- Native `phoenix_content_tests` built and `system.content-and-packs` passed (1/1, 0.55 seconds); logs: `content-agent-tests-build.log` and `content-agent-tests.log` in the same directory.
- A static comparison of 220 original public bodies found only a moved private diagnostic helper replaced with the identical exception text. This supports inspection and is not substituted for execution evidence.
- Early compile iterations caught a moved image-codec constant and an omitted explicit Controller compiler import; both were corrected without changing gameplay or verification rules.
- The first integrated configure exposed repeated dependency traversal on the expanded DAG. The guard now walks each reachable target once per invocation; synthetic diamond/conditional/alias cases passed, including graph mutation between calls. The architecture rules were retained.

This receipt verifies content's native seam and existing content contract. Browser validation, integration with all consumers and final facade retirement remain outstanding.

## First authoritative checkpoint

The first integrated authoritative build includes independently compiled control,
knowledge, fixed-point state/spatial, flight, fitting, resource, native outcome,
effect contracts, rules, abilities, processes, grants and transaction owners.
World still contains native domain adapters and several substantial fragments;
this checkpoint does not establish completion of those chunks.

- Native `phoenix_store_effects_tests` built successfully; log:
  `build/modularisation-baseline/authoritative-build-06.log`.
- `system.store_effects` passed (1/1, 0.84 seconds); log:
  `build/modularisation-baseline/store-effects-checkpoint.log`.
- Its new continuation trace, `store_effects-after.trace`, matches the frozen
  starting-revision `traces/store_effects-continuation.trace` byte for byte.
- The architecture checker's synthetic source/conditional/transitive dependency
  cases passed; log: `boundaries-checkpoint.log`. The full source scan still has
  explicit migration exceptions and is not a completed architecture gate.
- Integrated UI builds exposed missing direct standard/library imports and MSVC
  failures involving exported inline JSON operations. Canonical serializers and
  document/widget implementations now compile out of line in their owners.
  Lower graphics/UI/application/Workshop libraries have compiled; the complete
  tools/native application gate remains outstanding at this receipt.

The current save schema remains the starting revision's schema 32. No schema
bump, tuning change or completion of deferred effects features is part of this
migration.

## First complete native checkpoint

The native executable links and the full native build succeeds. All 46 existing
system cases and both architecture cases passed across the full run and one
focused rerun. The initial run passed 47/48: a mechanical path rewrite had changed
the sensor test's degree-symbol literal. Restoring the exact baseline literal
made the sensor case pass; its assertion was not relaxed. A token/literal audit
of all 63 changed existing test/manual/support bodies found no unexpected body
changes after accounting for four reviewed API/empty-namespace adaptations.

- Native build/link: `native-link-18.log`; full build and 47 checks:
  `native-checkpoint-19.log`; restored sensor check: `sensors-rerun-20.log`.
- Native build check, all 23 continuation journeys and all 12 live-network cases
  passed; log: `native-checkpoint-remaining-20.log`. Every regenerated trace is
  byte-identical to starting revision `34883ac`; fresh-file/hash receipt:
  `baseline-trace-comparison-20.json`.
- All 31 launcher integration tests passed; log:
  `web-launcher-checkpoint.log`. This is JavaScript launcher evidence, not a
  WebAssembly compilation pass.
- Six additional effect preparation/funding/binding/validation/clock targets
  passed standalone compilation before activation. Sensor evidence, coverage,
  acquisition, psychic projection/reception and network observation projection
  owners also passed standalone compilation. Their existing system cases are
  included in the native receipt above.
- Production base and mission packs regenerated and verified through the real
  packager during the full build. Authored production content was not changed.
- Windows final linking exposed the shared names `CloseWindow` and `PlaySound`
  in raylib and Windows import libraries. Native composition explicitly links
  presentation/audio before raylib and platform import libraries; no duplicate
  symbol suppression or runtime workaround was introduced.

The remaining 104 exact source-boundary exceptions identify unfinished native
composition/domain fragments. Additional domain owners are prepared separately
and are excluded from this verified checkpoint until compiled and activated.
Browser/Linux builds, final dependency-policy enforcement, remaining extraction
and graphical/device/screen-reader smoke evidence are still outstanding.

All logs and local preservation audits named here are under
`build/modularisation-baseline/`; they are execution evidence, not shipped
content goldens or completion claims for deferred gameplay features.

## Ordinary browser frontend checkpoint

Ordinary WebAssembly `client` and `workshop` now compile and link through the
new module graph; log: `wasm-frontends-24.log`. This exposed three portability
issues not diagnosed by the native compiler: a missing direct `<set>` include,
two pure platform interfaces without the common exception setting, and three
Workspace helper bodies defined in a shared private class header. The header
was added, the platform interfaces now propagate the established exception flag,
and the helper bodies have one implementation owner in the workflow module.

This is compilation/link evidence for the two frontend roles. Authoritative
WebAssembly execution, the threaded configuration and actual browser rendering
remain separate pending gates; the native counterpart of these fixes is checked
in `native-browser-fixes-25.log`.

## Second native ownership checkpoint

Twenty requested standalone targets passed before one guarded source switch
activated the next owners. These include immutable World entity assembly;
Controller protocol, coordination facts, handover and views; Away registry,
boarding and crew policy; directed/ordnance/weapons/continuation; physical
processor components, machinery/flight work books, finite work and process
validation; and sensor findings, projection and continuation validation.

- Full native build/link passed: `batch2-native-build-28.log`.
- All 48 native system/architecture checks passed in one run (339.21 seconds):
  `batch2-native-tests-29.log`.
- All 23 freshly regenerated continuation traces exactly match revision
  `34883ac`, retaining schema 32: `batch2-native-traces-30.log` and
  `batch2-trace-comparison-30.json`.
- Source ownership passed with 102 remaining exact migration exceptions:
  `batch2-source-boundaries-28.log`. Two removed state-header edges were retired;
  no new private-boundary exceptions were introduced.
- Integration compilation caught an optional-view/pointer mismatch, two old
  sensor registry reads, and a substring replacement in a phase helper name.
  These call sites were corrected. The preparation helper now matches identifier
  boundaries; a read-only audit found no remaining identifier/string corruption.

The live-network group was not repeated at this intermediate checkpoint: all 12
passed at the preceding checkpoint, networking implementation was unchanged,
and the final integration gate will rerun it. Authoritative/browser execution
and the private World implementation split remain pending.

## Generic ownership and environment activation

State trees, vocabulary, knowledge records, resource networks, conversations,
flight definitions and scenario execution now have module-local targets.
`phoenix_gameplay_definitions` and `phoenix_gameplay_runtime` are compatibility
INTERFACE aggregates rather than shared source owners. Definition-only consumers
link the appropriate definition target; execution consumers add the matching
runtime target.

`phoenix.scenarios.environment` independently owns region/point/interval types,
region authoring compilation, exact geometry, attenuation and deterministic
asteroid generation. It depends only on foundation. Environment implementation
no longer includes the scenario runtime private header. Scenario compilation
still hashes the original raw regions JSON alongside scenario/entity/slot data.
A static comparison found the region compiler and public geometry/field
algorithms unchanged apart from the coordinate validator's ownership/name.

The activated graph was exercised by `integrated-build-07.log`; that tools build
stopped at an unrelated viewport dependency omission, subsequently corrected by
the integration owner. This receipt is not a native or browser gate pass for the
new graph. The next integrated build and existing scenario/resource/knowledge
continuation cases remain required.

Production sources now import narrow content interfaces. Content definition
targets and module-local runtime consumers also name their specific generic
dependencies. The content facade remains available for compatibility callers
such as existing tests. The ledger preserves all 342 baseline origins, records
mixed-source splits explicitly, and adds separate entries for new owner files;
statuses distinguish the earlier native content checkpoint from pending wider
integration.
