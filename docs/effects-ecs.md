# ECS ownership of active effects

26 October 2026. Integrates the ECS effects refactor with the five shared-rule and ability-consolidation commits ending at `bcdb5e5` on `codex/shared-rules`. Those commits were rebased onto the effects work and integrated on `main`; the combined runtime adaptation is `35a8f37`. The original source branch remains available.

## Ownership boundary

Each modifier application is a Flecs entity with source/definition identity, a resolved target, expiry and its actual named numeric values. Several values supplied by one application share that entity. Maintained rule applications use this representation too; the native officer, equipment or region remains the owner of its source rule clocks and eligibility.

Lasting conditions are Flecs entities carrying `ActiveRuleEffect`, including their captured origin and private clocks. Capability queries select these entities. Query-derived ordered handle caches preserve logical instance ordering; addressed clock lookup uses an instance-to-entity index rather than searching a world vector. No effect entity has a scene-body identity, pose or radar presence. Source provenance does not create a cascading ownership relationship to a live caster.

Native shields retain their reserves, warmup, funding and contribution ownership. Ability execution, recovery, resources and personnel facts remain with their existing owners. Instant operations remain transactions. Scheduled payload, shield and other reactions use the shared rule queue introduced by the incoming branch; the retired private periodic/payload scheduler is not restored.

## Properties and transactions

Native property reads use numeric property keys and aggregates indexed by equipment handles, shield contribution identities or logical away participants. Independent sums are cached. Rule-owned sums retain source grouping, and current rule/source eligibility is checked through a query-local availability cache. Health, assignment, deployment and detention changes therefore remain visible within the same tick without rebuilding officer trait strings on every property read. Shared maintained rules supply the officer applications; there is no second trait-execution path.

`PropertyEffects` is a transaction/persistence snapshot, not a live ECS component. `PropertyEffectOwner` holds allocation state and derived handle/aggregate indexes. The shared prepared effect transaction retains its original-state and epoch checks, ordered invocation batches, payments and rollback behavior. It snapshots a touched property owner once per quote. Commit updates only changed ECS values/lifetimes and invalidates numeric aggregates when their values or membership change. Filtered role/property subsets, computed values and finite maintained lifetimes remain supported.

The review follow-up uses grouped `ModifierApplication` snapshots directly for preparation and restoration. It removes flat per-value ownership rows and their regrouping adapters, including an unused Personnel read adapter. Property keys are shared with the content catalogue; each descriptor explicitly identifies its key, and native descriptor lookup is independent of catalogue ordering.

Read-only condition passes iterate the cached ordered handles directly. The custom const/mutable iterator and its implicit handle-vector copies are removed. Condition transaction preparation captures the original roster once and derives the staged copy from it; exact owner removal checks the existing instance index before preparing a snapshot. Target pruning checks changing equipment generations, shield contribution identities and away assignments without copying or filtering immutable modifier definitions. Static value/target compatibility remains enforced on application and restoration.

Modifier expiry and target reconciliation retain their native phase boundaries. Lifecycle queries continue processing effects whose physical target is disabled. Resolved Flecs generations prevent a deleted target or condition from accidentally addressing a recycled entity. Expiry, refresh, removal and source disappearance retain their distinct contracts.

The integrated shared scheduler freezes due rule evaluations before committing a batch. Removing a condition stops subsequent scheduling; it does not retract an already frozen event in that batch. New work follows the existing phase/tick scheduling rules. The regression covers removal while another effect entity is created, frozen damage executing once, and identical resumed continuation.

## Continuation

World and portable session schema **28** supersedes both branches' incompatible schema-27 layouts. Earlier saves require a new session. Content and transport schemas are unchanged by this ownership refactor.

The `active_effects` record contains:

- `modifier_owners`: stable native owner IDs and their next application IDs.
- `modifiers`: one record per application, with named live values, maintained ownership and target identity.
- `conditions`: lasting rule effects with captured contexts, expiry and clocks.
- `next_condition`: the next lasting-condition identity.

Source rule clocks, region membership, prepaid work, provenance, pending events and receipts remain in the shared `rules` record. Active applications are not duplicated there or in body records. Flecs handles, query caches and property aggregates are rebuilt rather than serialized. Restoration validates a replacement world, including maintained-grant ownership, before replacing the live world.

The review follow-up keeps schema 28 unchanged. Conditions are serialized directly into `active_effects` and validated from that location, without translating them through retired fields in `rules`.

## Verification

The review simplifications are recorded in `12220bd` (condition iteration, transaction staging and dynamic target pruning) and `da8d841` (shared typed property keys, grouped modifier snapshots and direct persistence). The affected native rules, directed, store-effects, scenario-world, Personnel, abilities and combat suites passed. All 44 native integration suites, launcher checks and authored-content/build checks passed. The initial native gate passed 11 of 12 network cases; the valid-sidecar pre-game lobby case timed out waiting for approval while the WASM build ran concurrently. That exact case passed in isolation without source changes after the build completed. The final two native traces were then generated, and all 21 regenerated continuation traces matched the pre-cleanup `0dc300c` outputs exactly. Local receipts are `build/effects-simplify-native-gate.log`, `build/effects-simplify-lobby-retry.log` and `build/effects-simplify-continuation-comparison.json`.

The full `./scripts/check.ps1 -Target web -Threaded` follow-up gate passed, including production launch verification, system suites and all 21 native/WASM continuation comparisons. Repository formatting and `git diff --check` passed. The WASM receipt is `build/effects-simplify-web-gate.log`. The profiling figures below remain evidence for their stated earlier revision; this cleanup does not claim new measured timings.

The integrated focused native suites passed for shared rules, store effects, directed systems, abilities, Personnel and combat. They include computed/filtered maintained values, mixed prepared invocations, rollback, finite lifetimes, source loss, malformed continuation and frozen periodic batches. The scenario suite additionally passed multi-value application identity, shield-target reconciliation and atomic rejection of malformed effect rosters.

`./scripts/check.ps1 -Target native` passed on the integrated runtime: all 44 CTest suites, the HTML launcher checks, authored-content/build checks, all 12 signalling/relay/native-network cases and continuation trace generation. `./scripts/check.ps1 -Target web -Threaded` passed the full Emscripten 6.0.12 threaded build and Node-based WASM gate, including production launch preflight, synthetic system suites and all 21 native/WASM continuation comparisons. The runtime revision is `1ca5825`; the multi-checkpoint trace helper correction is `9d4f92b`. Logs are retained locally at `build/effects-integrated-native-gate.log` and `build/effects-integrated-web-gate.log`.

Repository formatting checks and `git diff --check` passed. These gates do not establish graphical, physical-device, screen-reader or six-display acceptance. Earlier standalone ECS captures use the older gameplay runtime and do not establish the integrated build's performance.

The integrated comparison uses the incoming shared-rule runtime at `bcdb5e5` as its baseline and the combined runtime at `1ca5825`, built with the same native Release toolchain. Read-only reaction validation now caches clock-capacity accounting rather than copying every active condition's clock map. Clock creation, removal, condition commits and address rebuilds invalidate that derived count; projected ownership transactions still validate their complete staged roster.

Paired captures reverse baseline/final execution order. Each configuration compares all four complete continuation states after normalizing only the schema number and the storage relocation of effect records. Physical values, quantities, clock state, pending work and receipts remain in the comparison. These comparisons are run-time evidence, not stored expected-world fixtures.

Whole-tick measurements on an Intel Core Ultra 9 275HX, native Visual Studio 2022 Release, in milliseconds (each entry averages the corresponding statistic from two runs):

| Input         | Applications | Ticks/run | Baseline median | ECS median | Baseline mean |  ECS mean |
| ------------- | -----------: | --------: | --------------: | ---------: | ------------: | --------: |
| Modifier      |           64 |       128 |           0.679 |      0.521 |         0.686 |     0.527 |
| Modifier      |          512 |       128 |           2.517 |      0.769 |         2.589 |     0.818 |
| Modifier      |        2,048 |       128 |           8.519 |      1.445 |         8.879 |     2.048 |
| Due condition |           64 |       128 |           7.484 |      7.829 |         7.508 |     7.860 |
| Due condition |          512 |       128 |         395.254 |    425.561 |       395.899 |   425.363 |
| Due condition |        2,048 |        16 |       8,362.218 |  8,667.737 |     7,951.978 | 8,260.853 |

Modifier reads improve materially as applications grow. Initial bulk application costs more: at 2,048 modifiers the averaged maximum increases from 35.122 ms to 73.310 ms, while the averaged p95 falls from 9.622 ms to 1.564 ms. The shared condition scheduler remains expensive and these condition captures show roughly 4–8% slower medians; ECS ownership does not establish a general speedup for rule execution. Every condition in this synthetic fixture is due every tick. The 2,048-condition runs use a bounded 16-tick sample; their tail statistics are descriptive rather than a reliable latency distribution. All 24 runs passed continuation comparison. The [raw receipt](effects-ecs-scaling-2026-10-26.json) retains individual measurements, executable and pack hashes, revisions and sample counts.

## Explicit synthetic profiling

The optional `phoenix_effects_fixture` target generates independent synthetic packs. It does not inherit production ships or balance values and is excluded from ordinary builds and CTest timing gates.

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_effects_fixture,phoenix_headless_profile
./build/native/Release/phoenix_effects_fixture.exe modifier 2048 build/effects-modifier-2048.zip
./build/native/Release/phoenix_headless_profile.exe build/effects-modifier-2048.zip test.map test.profile-ship 128 build/effects-modifier-2048
```

Use `periodic` instead of `modifier` for due-condition work. Compare the same generated pack, toolchain, tick count and machine. The headless diagnostic reports whole-tick mean, percentiles and maximum, including initial application work; it saves continuation outside the timed region. Compilation, networking and other checks must be stopped while measuring. These measurements do not establish graphical, accessibility, physical-device or six-display acceptance.
