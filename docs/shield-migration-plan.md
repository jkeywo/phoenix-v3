# Shield migration implementation plan

23 October 2026 · planned; no implementation or acceptance claimed

This plan implements the first slice of the [shared gameplay engine](gameplay-engine-design.md), using the decisions resolved in the [shield review](gameplay-engine-shield-review.md). The [migration notes](gameplay-engine-migration.md) retain the wider overhaul boundary. All stages below are pending.

## Deliverable and boundaries

The deliverable is one native shield implementation backed by ship-owned contributions, shared effect definitions and explicit source bindings. Existing fitted shields and an authored temporary shield use that same implementation, appear through the same permitted observation interface and participate in the same physical queries.

Preserve existing warmup, off-state recharge, power allocation, discharge conversion, faults, structural disablement and command ownership except for the expressly agreed changes: named contributions, additive named-property modifiers, equal-layer sharing and damage hooks.

Include catalogue/Workshop support, maintained content migration, native/WASM continuation and the dependent sensing/away/UI paths. The slice is not complete when only absorption works.

Defer the general resource/recipe database, arbitrary paid non-equipment consumers, full officer migration, all possible custom properties, weapon/directed-system migration and compartment/System damage hooks. Retuning is not introduced by this migration. Capacity, initial strength, arc, layer, warmup duration and conversion parameters stay fixed for each active grant. Reject unsupported authored operations clearly instead of silently approximating them.

## Agreed contracts to preserve

| Area                | Contract                                                                                                                                                                                                          |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Lifetime            | Installation creates an equipment grant once. Disablement, power loss and damage change operation without resetting reserve. Removal/expiry removes only the relevant contribution. Refresh never refills it.     |
| Identity            | An active-effect instance plus a stable local name identifies a contribution. Each contribution owns one arc, reserve and warmup state. Original source identity is separate.                                     |
| Funding             | The granting process owns costs/dependencies. Retain fitted equipment's power adapter; the independent demonstration grant is cost-free. Discharge requires an explicit destination and conversion rule.          |
| Observation/control | All contributions use permitted shield observations. Equipment commands retain their bindings; other grants expose controls only when authored. Visibility does not grant control or reveal private source facts. |
| Live properties     | Recharge/off-state recharge rate, sustain cost, discharge rate and available protection. Comparable percentage modifiers add once. Structural disablement remains an operating condition.                         |
| Hook scope          | Per contribution, only when damage reaches its layer. Native geometry and sharing determine presented damage. Context identifies the hit and contribution.                                                        |
| Pre-hooks           | Modify or negate assigned damage only. Negated damage is removed, not forwarded as bypass.                                                                                                                        |
| Reactions           | Secondary effects run after native resolution as separate executions. Secondary damage is scheduled, never recursively resolved inside the active hit. Failure cannot undo the original hit.                      |

## Runtime and content shape

These are implementation boundaries and suggested type names, not a mandate for a separate ECS entity for every record. Final names and encodings are frozen with the first stage's compiler contracts.

| Record/interface            | Required responsibilities                                                                                                                                                               |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Compiled effect definition  | Stable catalogue ID; named native shield grants; typed fixed values, live property defaults, lifetime policy and the supported hook declarations. No per-tick JSON.                     |
| Active effect record        | Persistent runtime ID, definition reference, original source key, target, expiry/dependencies and captured application values. Separately identify creation, refresh and removal.       |
| Ship shield component       | Owns contributions and native queries for coverage, absorption and observation. Its lifetime does not depend on there being a fitted shield System.                                     |
| Shield contribution         | Stable local key, fixed arc/layer/capacity/conversion values, strength, warmup, recharge/discharge remainders and conversion credit, plus effective live properties.                    |
| Equipment grant binding     | Maps the equipment's operating intent, fault/structural state, payer and discharge destination onto its named contributions. Owns no duplicate shield reserve.                          |
| Observation/control binding | Explicit permitted feed and optional operating command mapping, independent of the granting source's identity.                                                                          |
| Pending damage/result       | Stable hit identity and contribution key; incoming/modified/negated damage, absorbed amount, residual damage and resulting shield state. Preserve attack lineage and bypass accounting. |
| Queued reaction             | Stable execution identity, originating hit/source, captured event values, targets, due phase/tick, operation group and any supported cost binding.                                      |

Use stable logical IDs in persistence and ordering, never Flecs allocation order or component addresses. Equipment source keys include the owning entity and fitted instance identity. Scenario sources use their existing instance/action lineage. Preserve bookkeeping for an independent grant whose source no longer exists.

### Creation and refresh

Equipment instantiation creates its grant once. A production scenario action applies, refreshes or removes a named grant through the same runtime service. Replaying an already committed action must not create a second effect. A new deliberate application can create an independent effect instance; refreshing addresses the existing grant rather than inferring identity from source/effect/target alone.

Create/remove the complete required contribution group atomically. Refresh preserves strength, warmup and fractional accounting. Since fixed fields cannot change during this slice, reject an attempted in-place refresh that changes them; an author can explicitly remove and create a new grant when that is intended.

Support a compatible ship with no original fitted shield. Query the shield component directly rather than relying on shield equipment, CombatMechanism or resource-network membership to discover protection. The first slice need not grant shields to arbitrary incompatible world objects; validation of component compatibility is not source authentication.

### Property and accounting rules

Keep fixed integer units and explicit remainders consistent with native/WASM execution. Compile types, bounds and expression compatibility before runtime. Validate conversion denominators as positive; a cost-free process is explicitly supported and does not need a fake battery.

Translate the existing officer recharge bonus to the named recharge property with additive-percentage arithmetic. Translate active output faults into explicit recharge/protection modifiers; multiple comparable modifiers add. Disabled/feed/destroyed hardware and structural disablement remain availability conditions. Retain fault generation, diagnosis, repair and officer assignment outside this bridge. Do not apply the old officer formula or general fault multiplier again after calculating the new properties.

Clamp nonnegative rates/costs at their valid bounds with checked arithmetic. Usable protection is bounded by actual remaining strength; a positive protection modifier must not make reserve negative or create free stored strength. Preserve warmup's contribution to usable protection. Initial capacity/strength and fixed conversion data are not live modifier targets in this slice.

One equipment process submits its cost once. When it supplies several named arcs, aggregate their recharge demand and partition the resulting funded budget deterministically; do not let every contribution spend the full same grant. Preserve sustain priority and the existing battery's headroom/charge-rate limits for discharge. Snapshot effective rates consistently across demand quotation and actuation so a modifier change cannot produce an unquoted output.

## Execution order and damage accounting

Retain the current Controller snapshot and native physical ordering in [simulation](simulation.md). Add explicit shield/effect processing points without giving Controller AI early access to physical results.

1. At a documented tick boundary, expire due effects and admit due grant changes. Publish their observations through the ordinary producer boundary.
2. Calculate operating dependencies and effective properties. Collect equipment shield demands and any supported due-reaction costs before preparing the existing resource allocation.
3. Prepare required operation groups, validate targets and costs, and commit funded work without partial groups. Actuate equipment shields with their grants; advance eligible independent contributions without requiring an equipment resource slot.
4. Native damage producers call the common shield resolver. Geometry, bypass and layer order remain native. Pre-hooks run at the mutation point, before the affected contribution absorbs damage.
5. Commit the native hit outcome, then create post-reaction records from that outcome. No secondary operation rewinds the hit or reenters it recursively.
6. Process secondary damage in an explicit reaction phase. Drain a frozen set of due reactions in stable order; work generated by that drain is eligible only at the next reaction phase, preventing an unbounded same-phase chain.

For the first implementation, make the reaction phase consume work due from preceding ticks, with newly generated reactions due on the next tick. This is a concrete first-slice schedule, not a global next-tick restriction on every future effect. Preserve due work and ordering across saves. Keep the existing destruction processing in the correct relationship to secondary damage so a newly destroyed object is handled once.

Pre-hooks in this slice have no independently spending or world-mutating action: they transform the pending damage context. Post-reaction groups use normal validation and atomic cost/application. Reuse existing resource bindings where a reaction declares a supported equipment payer. Stage the required writes and charge together; missing targets or insufficient funding leave the reaction unapplied and uncharged. Do not mistake an execution receipt for a transaction or spend the same preallocated grant twice.

### Layer resolution contract

Apply bypass once per incoming hit, retaining continuous-damage fractional carry. Visit covering layers in deterministic order. Equal-layer contributions share presented damage evenly; use stable contribution identity to distribute indivisible integer residue. Account explicitly for depleted/weak peers and redistribution before advancing to the next layer.

The implementation must specify the allocation/rebalancing procedure before coding the hook loop. It must not repeat logical hook side effects merely because native allocation uses several passes. Aggregate each contribution's actual result for its post reaction. Negated damage is removed from the pool being distributed; it cannot reappear as another contribution's share. Contributions in unreached layers have no pre or post events. When no pending damage remains, stop.

Preserve native collision damage's current route; adding shield hooks must not silently make shields protect against collisions that previously bypassed them. Keep damage absorption separate from visual emission so failed/prepared operations cannot emit an uncommitted shield flash.

## Staged implementation

Each stage is a verified logical change, committed with its evidence. Do not interpret the stage list as completed work. Keep unrelated concurrent repository changes out of these commits.

### 1. Add the reusable effect definition and compiler contract

**Dependencies:** none beyond the agreed design.

Extend `Catalog` and pack resolution with an effect table using existing dependency, override and inheritance rules. Define the minimal shield-grant schema, local contribution names, equipment binding and supported scenario/hook operations. Validate resolved references, types, units, bounds, names and unsupported fields. Resolve references before equipment placement/compilation so mod overrides affect consumers rather than flattened stale copies.

Enable authored operation vocabulary only alongside its execution support. In particular, hook-bearing active definitions remain rejected until stage 4 implements their semantics; an intermediate build must not accept and silently ignore them.

Carry effect references through scenario dependency closure, Workshop draft resolution and relevant export/client-asset paths. Expose effect definitions and reference selection through ordinary Workshop editing; reuse existing document history, validation and Test workflow. Retain authoritative/presentation separation in client content.

**Main seams:** `src/content/content.cppm`, `catalog.cpp`, `equipment.cpp`, `combat.cpp`, `scenario.cpp`, `workshop.cpp`, `console_assets.cpp`; relevant `src/tools/` editors.

**Gate:** synthetic pack fixtures load one shared effect used by several equipment templates, including a mod override and explicit local specialisation. Invalid effect references, duplicate contribution names and unsupported hooks fail with useful paths. Workshop/export/reload resolves the same effective definitions. Existing content still loads; no runtime path is claimed by this stage alone.

### 2. Move existing shields to the ship-owned runtime

**Dependencies:** stage 1.

Introduce the active grant/contribution owner and equipment adapter. Initialise fitted grants once; move all strength, warmup and fractional state out of `CombatMechanism::ShieldState`. Preserve accepted enable/discharge intent and native funding/thermal integration. Apply the agreed named-property bridge. Update resource quotation and grant partitioning for multiple arcs under one payer.

Switch absorption, coverage queries, commands, own-ship projection, Controller facts, scanner findings, displacement checks and shield visuals to the new owner. Equipment-facing adapters can preserve current command/feed IDs during this transition, but must read the ship component and own no second copy of shield state.

Implement dynamic-instance continuation now: grant IDs, original source, fixed/captured values, expiry, state and allocator counters. Update all save-version gates together using the current integration branch's next schema, and retain transactional restore. Do not hard-code an assumed version from the earlier review if other work has advanced it.

A temporary compiler conversion may lower the old inline shield fields into the same compiled effect representation until maintained content is migrated. It is a load-boundary conversion only; there is one live shield model. Remove this conversion in stage 5.

**Main seams:** `src/simulation/world.cpp`, `combat.hpp`, `combat_effects.hpp`, `combat_execution.hpp`, `gameplay_resources.*`, `controller_actions.hpp`, `sensors.hpp`, `away.hpp`; `src/core/console.cpp`, `sensor_projection.hpp`, `src/session/local.cpp`, and equipment/controller content bindings.

**Gate:** existing synthetic shield journeys pass against the new owner: off-state recharge, reserve-preserving enable cycles, funding shortages, discharge conservation and exact fractional continuation. Add a multi-contribution payer case and additive officer/fault cases. Existing command ownership, sensor knowledge and displacement behaviour remain intact. No active shield state remains duplicated under fitted equipment.

### 3. Prove an independent authored source and common observation binding

**Dependencies:** stage 2.

Connect production physical/scenario apply, refresh and remove operations to the effect service. Keep Controller AI's vocabulary confined to admitted commands. Use action lineage/receipts and stable grant handles for replay protection and explicit refresh/removal.

Add permitted ship-component shield feeds and optional operating bindings independently of equipment ownership. Equipment grants keep their established controls; temporary scenario grants are visible through the authored shield feed and read-only unless a control is declared. Ensure an empty compatible ship can receive its first shield without inventing equipment or a power network.

Use a cost-free finite-duration scenario grant to prove source independence. It has its own captured initial state and independent lifetime. Removing/expiring it must leave equipment-backed grants unchanged; restore must not replay creation. Respect target destruction/removal without dereferencing stale handles.

**Main seams:** scenario vocabulary/compiler and `src/simulation/gameplay_scenario.cpp`, `world.cpp`, common effect runtime, observation schema/projection and Workshop scenario authoring. Reuse supported scenario actor binding rather than adding a special test injector.

**Gate:** a synthetic production-loader scenario applies two sources to the same ship, refreshes one, disables equipment, expires/removes the other and resumes from a save. Both sources affect absorption, own-ship permitted observations, remote scan findings and displacement blocking. No command or private source information is exposed merely by being visible. A ship with no original shield fit can receive the independent grant.

### 4. Add reached-layer hooks and deterministic reactions

**Dependencies:** stages 2 and 3.

Implement the pending-hit/result boundary, equal-layer sharing and supported pure pre-hook transforms. Preserve once-per-hit bypass and retain hit/contribution identity. Add post-reaction operation groups and the persistent scheduled reaction queue. Initially support the native operations needed to demonstrate reusable grant application/removal and secondary damage; reject other unimplemented effect primitives. Use the existing equipment payer boundary for supported reaction costs, without introducing arbitrary dynamic resource consumers.

Specify and implement phase ordering, stable event ordering, event retention, bounded reaction storage and explicit handling of exhausted limits. Never silently drop paid or committed work. Validate finite pre-hook expressions and their numeric limits. Stage required effects and costs together; record execution success only after the group commits. Snapshot due work so reactions cannot recursively drain themselves.

**Main seams:** shared shield/effect runtime, `combat_effects.hpp`, `combat_execution.hpp`, `combat_ordnance.hpp`, `world.cpp`, `effect_receipts.hpp` or a shared receipt interface, scenario/effect compilation and continuation.

**Gate:** synthetic scenarios cover pre-damage modification and negation, per-contribution post results, no hooks on unreached layers, weak/equal-layer peers and integer residue, reaction cost failure without rollback of the hit, secondary damage deferred to the scheduled phase, and replay-free resume with pending reactions. Include a source disappearing after an independent result is queued. Native/WASM continuation must match for the same synthetic run.

### 5. Migrate maintained content and remove the old shield path

**Dependencies:** stages 1–4.

Extract reusable shield effect definitions and update maintained equipment/assembly references, command/feed bindings and shield-specific tests. Preserve authored tuning except the agreed semantics; prefer shared template inheritance so a mod can make a sweeping shield change in one place.

Discover the maintained pack set from the current build/content configuration, including production, development and bundled mission packs. Packaging is being changed concurrently; do not assume the pack roster from the review is still complete.

Remove old inline shield lowering, legacy shield state/variant cases and old shield-only modifier/reader paths once all maintained content and fixtures use the new model. Keep equipment identity where commands, faults or repair still legitimately need it. Update content/authoring documentation, shield gameplay documentation and delivery evidence to distinguish implemented contracts from outstanding manual acceptance.

**Gate:** full resolved-catalogue verification passes; representative maintained fits load through the new definitions; Workshop Test/export/reload works with the new references; no legacy shield execution path remains. Complete the native/WASM automatic gate and record its actual results before claiming the slice complete.

## Validation and evidence

Follow [testing policy](testing.md). Extend existing synthetic tests where they already own the boundary; use a dedicated shield/effect integration fixture only where it makes the new source/lifecycle path clearer. Fixtures must use the production loader and must not inherit production ships or balance packs. Do not add one test per helper or freeze shipped tuning.

| Boundary              | Required evidence                                                                                                                                                                   |
| --------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Catalogue/mods        | Shared-reference override, inheritance/local specialisation, invalid references, draft/export/reload equivalence.                                                                   |
| Lifetime/identity     | No reset on disable, funding loss or refresh; independent arcs and sources; selective removal; no duplicate grant after replay.                                                     |
| Accounting/properties | One payer budget spent once across arcs, sustain priority, recharge and discharge fractions, no reserve underflow, additive negative/positive modifiers without double application. |
| Physical damage       | Arc coverage, layer order, equal-layer handling, bypass once, pre-negation removes damage, post result matches committed state.                                                     |
| Reactions             | Independent failure, supported paid reaction commits atomically, deterministic deferred secondary damage, pending queue and receipts survive restore.                               |
| Knowledge/control     | Same permitted human/AI view, explicit control bindings, normal remote acquisition/staleness, non-equipment grant blocks displacement where it covers.                              |
| Persistence           | Dynamic grants without a fitted roster entry, removed-source bookkeeping, fractional credit, malformed save refusal without changing the live world, native/WASM parity.            |

Build and run the affected tests after each stage. Existing relevant suites include content, combat, personnel, sensors, away/boarding, Controller/runtime, scenario World and local-session continuation; inspect current CTest registration rather than relying on stale test counts. Broaden to the complete automatic gate at integration:

```powershell
./scripts/check.ps1 -Target native
./scripts/check.ps1 -Target web
```

Use the current [build instructions](building.md) for prerequisites, cross-machine trace transfer and platform-specific execution. The web gate must compare traces from the same native revision and fixture run. Extend the synthetic combat trace or add a dedicated effect trace when necessary to include temporary grants and pending reactions; do not store a production-content golden.

Graphical journeys are separately and manually triggered: ordinary shield controls, the visible read-only temporary grant, source-specific control availability, scanner display, Workshop authoring and accessible status/action presentation. Physical screen-reader, multi-display/device and performance evidence remains separate from headless checks. Record unrun acceptance instead of treating a build as proof.

## Completion checklist

- [ ] Shared shield effect definitions resolve consistently in runtime, Workshop, export and selected mod packs.
- [ ] Equipment and scenario grants use the same native component with independent named state.
- [ ] Funding, lifecycle, modifiers and discharge obey the agreed accounting contracts.
- [ ] Every physical reader and permitted projection includes contributions from all supported sources.
- [ ] Reached-layer hooks and queued reactions obey cancellation, ordering and atomic failure semantics.
- [ ] Dynamic continuation and native/WASM parity are verified without replay/refill.
- [ ] Maintained content is migrated and old shield execution/lowering paths are removed.
- [ ] Automatic gate evidence is recorded; manual acceptance is recorded separately with any remaining limits.

Commit each verified logical stage. No gameplay implementation is performed by writing or approving this plan, and no stage is complete until its gate has actual evidence.
