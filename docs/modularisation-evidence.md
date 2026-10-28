# Modularisation migration evidence

Work started 27 October 2026 at 23:12:41 UTC from `34883ac` with a clean working tree. The user requested the full [migration plan](modularisation-plan.md), targeting approximately ten hours while preferring correctness to a hard deadline. No push or deployment is authorised by this migration.

## Execution approach

Root coordinates source ownership, CMake integration, validation and commits. Parallel work is divided between content, UI/graphics, Workshop/application, World/control/knowledge, effects, and dependency enforcement. Shared World integration has one owner; compiler interfaces and state contracts are coordinated explicitly.

The initial relocation is one mechanical integration step across all existing sources. This deliberately precedes the numbered domain extractions to let parallel workers use stable final paths and to avoid repeating global path edits during interface work. Relocation is recorded separately from extraction: an included fragment in its destination folder remains unfinished and is not counted as a completed module.

Current compiler functions contain genuine strongly connected call groups (including effects/abilities/processes and combat/directed definitions). Narrow declaration and compiler interfaces will be split first; mutually recursive implementations may temporarily share an explicitly documented compilation target. This is not permission for interface-import cycles, duplicate declarations or permanently unrestricted dependencies.

## Chunk 00 - baseline

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

## Third native extraction checkpoint

Damage allocation, repair work policy, ordnance contacts, clinical treatment,
psychic contribution reconciliation, interference selection and native emission
measurement now compile as independent providers. The existing periodic-clock
owner advances due frontiers, and process-expression evaluation lives in the
process owner. Native adapters delegate to those providers without changing
physical payment or event publication order.

Unused content/gameplay umbrella targets and Core were retired. Their consumers
now name actual owner targets. The first integrated build exposed two test
consumers of Console free functions missed by the import audit; their imports
and explicit model dependencies were restored without changing assertions.

- Complete native build: `batch3-native-build-33.log`, passed.
- Native system and architecture suite: `batch3-native-tests-34.log`, all 48
  passed in 541.78 seconds.
- Source ownership: 588 C++ sources, 199 module units, 102 exact temporary
  exceptions, zero additional violations. These exceptions remain unfinished
  extraction work.
- Existing test-body comparison against `34883ac`: 62 modified existing test,
  manual and support bodies checked; zero unexpected token/literal differences.
  The four previously documented API adaptations are accounted for explicitly.
- Fresh continuation comparison: all 23 traces are byte-for-byte identical to
  `34883ac`; receipt `batch3-trace-comparison-36.json` verifies fresh write times
  and records each digest. The native build check also passed.
- Non-ASCII source-literal audit: all 47 baseline occurrences remain unchanged,
  with no added or removed distinct literals. This checks encoding preservation
  separately from the runtime traces.
- Formatting check passed after correcting three reported files: 725 C/C++,
  147 CMake, 11 PowerShell and 412 web/data/document files checked.
- The full ordinary WebAssembly build found a Clang dependent-template lookup
  error in the new ordnance binding. Added the required `template` keyword;
  the native simulation counterpart rebuilt successfully in
  `batch3-native-portability-38.log`. The complete ordinary WebAssembly retry
  passed in `batch3-wasm-build-37.log`, including the authoritative server,
  client, Workshop and all test binaries. The unchanged browser runtime gate
  and native/WASM continuation comparisons are running separately; compilation
  alone is not recorded as a runtime pass.

Ubuntu is installed under WSL, but the inspected environment lacks Clang, CMake,
Ninja and PowerShell. Native Linux module compilation remains unverified.

## Fourth ownership checkpoint

The integrated graph now includes property application and active-rule stores
with their real ECS queries, a flight actuator service with four owned queries,
the Away lifecycle and its canonical staging/recovery policies, and the full
interdiction book/tree driver/order planner. World supplies typed physical and
cross-domain operations at the original execution points. Profiling and the
native/browser Workshop diagnostic bridges also have compiled owners.

The flight service borrows the machinery book only for a phase call; it retains
no reference whose destruction order could outlive that book. Away read paths
use const actors and separate execution-local bindings, with no mutable ECS or
peer actions. Independent reviews found no concrete effects or interdiction
ordering/continuation regression.

- Independent native provider compilation passed before adapter activation.
- Complete native integration build: `batch4-native-build-48.log`, passed.
- Complete native system and strengthened architecture suite:
  `batch4-native-tests-50.log`, all 48 passed in 352.37 seconds.
- Fresh continuation run: `batch4-native-traces-51.log`; all 23 traces exactly
  match `34883ac`, with freshness and digests recorded in
  `batch4-trace-comparison-51.json`.
- Dependency ceilings, complete leaf coverage, raw-system-library handling and
  final deferred frontend/platform/runtime barriers are enforced. Native
  configuration and the expanded positive/negative fixture suite passed.
- Source ownership: 606 C++ sources, 209 module units, 97 exact temporary
  exceptions, no additional violations. Five retired exceptions were removed;
  remaining World fragments still represent unfinished separation.
- Every current code folder and family has a reading guide. The reading map and
  counts are snapshots; final counts must follow outline/retirement cleanup.

The ordinary browser runtime gate (`batch3-wasm-runtime-40.log`) reached the
Controller case after the preceding subsystem cases passed, then trapped on an
out-of-bounds access. A symbolized relink of the same object code located the
failure in `EquipmentRange::operator[]` called by Controller coordination facts.
The newly introduced range expression dereferenced a temporary optional view;
the optional died before iteration. The loop now names that view in its C++20
initializer for the entire iteration. The other uses of both fitted-view APIs
were audited. Native tests pass with the correction; the actual browser case
was rebuilt successfully (`batch4-wasm-controller-build-49.log`) and passed its
full system journey (`controller-wasm-fixed-53.log`). Its continuation digest
`dfcd28c4d881f9569fca360b2df832660b3b9012b7fd26eb6987f2146deb44fa`
matches the fresh native trace. This is a targeted browser pass; the complete
ordinary and threaded browser gates remain required after the final graph.

The first integration build also exposed an unconditional Emscripten exception
flag on the new profiling owner. Its propagation is now restricted to
Emscripten, and the corrected native build has no such linker warning.

Formatting check passed for 743 C/C++, 150 CMake, 11 PowerShell and 439
web/data/document files (`batch4-format-check-52.log`). The source ledger still
retains all 342 baseline origins and separately records new owner files.

## Aggregate retirement activation

The unused `phoenix.content` re-export interface and its build target are now
removed. The generic gameplay definition/runtime compatibility targets and
`phoenix_core` are also removed. Their callers name actual module targets. World
retains the exact seven generic runtime dependencies from the former alias;
this retirement does not alter authoritative phase order or execution.

Application shell, navigation and Console model consumers now link only the
services their APIs use. The synthetic case loop no longer acquires all three
through a blanket dependency; the five cases using Console/navigation add
those owners explicitly. Unused imports were removed with their dependencies.

Source/CMake inspection found no active references to the retired targets or
content facade imports. Focused formatting and whitespace checks passed. This
receipt records activation only: the post-retirement native/browser builds,
runtime suites and continuation comparisons remain integration-owner gates.

The first post-retirement full build exposed two Console free-function callers
missed by the initial type-focused dependency audit: craft eligibility and
knowledge message formatting. Their explicit Console imports and model target
links were restored without changing test assertions or behavior. The full
Console public function/type consumer list was then inspected, including calls
inside test helper headers; the retry remains an integration-owner gate.
