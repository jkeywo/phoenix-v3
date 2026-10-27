# Machinery process migration evidence

Implementation `7e155da1abcbd7c9215bf40d64039488809c320a` follows the [approved eighth-slice plan](machinery-process-migration-plan.md), based on `e127b52cb69af867bc85b34af2b8b21b5b08c0e0`. The [authoring reference](machinery-process-content.md) describes the concrete content contract. This record separates executed checks from manual acceptance.

## Delivered scope

Generation, propulsion (including boost and impulse), cooling and existing recipe processors use named shared processes. Entity, equipment, officer and compartment owners support effect-only execution and temporary grants. Requirements, exclusion groups, costs, expressions, work effects and lifecycle consequences reuse the existing rule and prepared-effect paths. Native machinery owns allocation, thermal protection, flight integration and recipe progress.

A paused process preserves its run and prepaid fractions. Start and resume consequences gate admission. Mandatory pause/stop remains effective when its optional exit consequence fails; that consequence commits neither fees nor effects and records failure through existing rule receipts. Producer inputs and expanded lifecycle fees are admitted from pre-generation quantities, including native electrical discharge limits and consumption heat. Downstream consumers can use accepted generation in the same tick.

Frozen work receipts, named Controller endpoints, permitted human/AI observations, Workshop editing, heat operations, maintained-source withdrawal and strict continuation are integrated. Production definitions retain their physical tuning and consumption; six implicit Courier engine/steering declarations are explicitly migrated. World and portable-session schemas advance together from 29 to 30; older sessions require a new game.

FTL jobs, repairs, Medical work and craft servicing retain their existing execution and are outside this conversion.

## Review corrections

Independent correctness review found destruction retirement, maintained process-grant withdrawal, retired-run credit publication, process-rule projection/visibility and fractional-rate retention gaps. These are fixed with synthetic production-loader regressions. Funding tests also exposed survivor caps retaining a provisional zero after a competing process was rejected; resets now follow genuine request-set reductions. Network projection retains frozen process-row order.

The first complete native gate passed 42 of 46 suites and exposed four remaining integration failures. Two synthetic pack rewrites dropped their process declarations; those fixtures now preserve/rematerialize their authored catalogue through the normal helper. A rejected launch could skip offering released ammunition to a processor before requotation; the retry now precedes every continuation edge and captures the newly prepared native request. Consumed craft now close their processes at the physical terminal boundary. Recoverable dormant craft pause native machinery rather than publishing stale flight work; effect-only execution remains independent of physical dormancy. The affected directed, content, fleet, Security, craft and process suites pass after these corrections. The complete gate is repeated below rather than inferred from those selected reruns.

The architecture review removed unused alternate process commit paths and unnecessary helper parameters. Preview and actuation share native flight-work arithmetic. Native resource availability owns thermal protection policy. Producer quotation uses the existing stock transaction, cost resolution and prepared-effect commit rather than another wallet, DSL or scheduler.

Independent correctness and architecture reviewers confirmed PASS on that immutable implementation against the stated base, with no remaining actionable findings. Reviews were read-only and reused the integrator's executed evidence. The later live-probe authoring correction is committed in `bcb0d910514bdad63eb8f7305387ecf782b02084` and independently reviewed. It changes only synthetic probe construction; assertions, timeouts, protocols and expected outcomes remain intact. Executed broad verification is recorded below.

## Physical predecessor comparison

The predecessor `e127b52` was built in an isolated local checkout with the same test-only snapshot helpers. Synthetic flight checkpoints exercise generation, cooling, native controls and propulsion; processor checkpoints retain claims, progress, resources and native counters. Both before/after JSON comparisons match exactly.

Normalization excludes top-level schema/archive/process ownership, removes only `processes` declarations from embedded flight-definition metadata, and removes newly introduced counters only when exactly zero: flight work remainder, generation/cooling throughput remainder and producer-input telemetry. All previous native motion, energy, heat, coolant, allocation and recipe fields remain compared; nonzero new counters remain visible.

The captures are `build/process-before/*-physical.json` and `build/process-after/*-physical.json`; `build/process-physical-comparison.log` records zero differences for both fixtures.

## Verification

The complete native game build and all 21 grouped process regression cases pass. Focused flight, craft, custom-property, psychic and processor suites also pass.

`scripts/check.ps1 -Target native` passed all 46 CTest suites, 31 launcher cases, the native build/content check and 21 freshly generated continuation files in `build/process-native-gate-2.log`. Its network stage passed 7/12 cases: the live-session and lobby probes built synthetic packs without the newly required process declarations. Those two probe sources now use the same explicit fixture authoring path as the other synthetic tests. A complete `scripts/check-network-service.ps1` rerun passed all 12 cases, recorded in `build/process-network-gate-2.log`. The original combined invocation is not reported as a passing single run.

That network-stage failure had prevented the final Controller and psychic reference files from being regenerated. Both were explicitly generated with their current native executables after the fixture correction, completing the 23-file native reference set. The first threaded gate passed its suites and production preflight, then stopped at the stale Controller reference. `scripts/check.ps1 -Target web -Threaded` then passed in full against the fresh references, including the complete threaded build, all system suites, production launch/content-refresh preflight and all 23 native/WASM continuation comparisons. `build/process-threaded-gate-2.log` records that passing complete invocation; `build/process-threaded-gate.log` preserves the earlier stale-reference failure.

The ordinary WebAssembly configuration was built with `scripts/build.ps1 -Target web -Threaded:$false` for content, processes, flight, processors, craft, custom properties, abilities, psychic work, rules, store effects and the Workshop-test preflight executable. All ten focused suites passed. Production launch and bundled-content-refresh preflight passed. All eight selected native/ordinary continuation comparisons matched: processes, processors, craft, custom properties, abilities, psychic work, rules and store effects. The ignored helper `build/check-processes-ordinary.ps1` records the exact commands; `build/process-ordinary-build.log` and `build/process-ordinary-gate.log` record their execution. This is focused ordinary-build evidence, not a claim that the entire threaded suite was repeated in ordinary mode.

Standard repository formatting and `git diff --check` pass (`build/process-final-format.log` and `build/process-final-format-check.log`). Final evidence text also passes its selected Prettier check (`build/process-evidence-format.log`).

The local Windows toolchain uses installed Node 24.13.0 through the scripts' expected SDK path. Dependency sources are reused from the primary checkout. Native network fixtures use a temporary short junction to the same native build directory to avoid Windows journal path-length failures; the validated junction is removed afterward. These are local ignored tool adaptations, not production changes.

Manual graphical journeys, screen-reader use, physical devices, six-display performance and Linux execution are not claimed by these automated checks. No measured performance improvement is claimed.

## Effect consolidation follow-up

Implementation `81e91d121bd660269ceed4e840b24ecf8a80450a`, reviewed independently against `b22f745`, implements the architecture review recommendations without changing authored schemas, save version, tuning or intended gameplay. Final payment staging now belongs directly to the existing prepared effect group; the redundant property/inventory maps and stock alias are removed. Fee quotation and full effect preparation share nested process-removal expansion, retaining prepaid subtree handling, root attribution, deduplication and capacity checks.

World preparation and verification reuse the already compiled Controller layout. Live and captured Controller observations share process-row interpretation. The existing property and process readers use one small native-owner lookup with their own state selections. Unused process-output/source helpers, an unused lookup and an unused observation argument are removed. Ability/process lifetime differences and the pre-generation resource view remain intact.

The six focused native process/property/ability/rule/stock/content suites pass, and the Controller suite passes separately under its exact CTest name. An independent review of the immutable source commit reported PASS with no actionable findings. Standard formatting and whitespace checks pass (`build/process-consolidation-format-check.log`). Predecessor continuation files are retained in `build/process-consolidation-before/` for a temporary before/after comparison, not a new production tuning golden.

The complete `scripts/check.ps1 -Target native` invocation passes, including all 46 CTest suites, 31 launcher cases, native build/content checks, all 12 live-network cases and generation of all 23 continuation traces. No heavy compilation ran during the network stage. The native log is `build/process-consolidation-native-gate.log`. All 23 refreshed native continuations match the pre-refactor references exactly (`build/process-consolidation-native-comparison.log`), including funding, lifecycle and Controller cases.

The complete `scripts/check.ps1 -Target web -Threaded` gate passes against those refreshed references, including production launch/content-refresh preflight, all threaded suites and all 23 native/WASM continuation comparisons. Its build and executed gate are recorded in `build/process-consolidation-threaded-build.log` and `build/process-consolidation-threaded-gate.log`. Final evidence text also passes its selected Prettier check (`build/process-consolidation-evidence-format.log`). This follow-up records native and threaded checks; the earlier focused ordinary-build results remain evidence for the original migration. No new graphical, accessibility, device or performance acceptance is claimed.
