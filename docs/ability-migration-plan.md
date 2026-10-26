# Shared abilities and psychic migration

26 October 2026 · implemented, independently reviewed and automatically verified. See [evidence and manual acceptance boundaries](ability-migration-evidence.md).

This is the fifth slice of the [shared gameplay engine](gameplay-engine-design.md), following the delivered [directed systems and payload effects](directed-systems-migration-plan.md). [Resource/store unification](resource-store-evidence.md) supplies shared stock accounting; the preceding directed slice supplies targeting, effect execution and periodic lifetime integration.

## Intended result

Officers and equipment grant reusable activated and sustained abilities. An ability defines requirements, targets, optional preparation, costs, effects, interruption and recovery. Shared machinery owns that execution lifecycle; native mechanisms own concealment, ghost projection, ward reception and other physical outcomes.

The first production migration is the existing cloak, scrambling and ward powers. Their gameplay remains as documented in [Psychic choir](psychic-choir.md), except for the agreed change that officer recovery follows the officer rather than the occupied choir seat. Synthetic fittings prove equipment abilities, apparatus-free officer abilities, preparation, concurrency restrictions and temporary grants without adding new production gameplay.

## Settled scope

- Build shared activated and sustained abilities, proved by migrating all existing psychic powers.
- A source may grant several independently usable abilities. Explicit exclusion groups declare combinations that must not overlap.
- Include optional interruptible preparation before activation.
- Recovery follows the granting officer or equipment across reassignment and temporary unavailability. Apparatus recovery is a separate constraint when authored.
- Include temporary abilities granted by effects, alongside abilities declared directly on officers and equipment.
- Reject activation that conflicts with a preparing or active ability. Starting another ability never silently stops the current one.
- Include catalogue/mod integration, Personnel and equipment bindings, Controller commands, standard Console presentation, Workshop, maintained content and continuation.

Routine passive officer buffs remain ordinary effect contributions. This slice does not migrate every engine, generator, region, fault, repair job or mission workflow into an ability. New psychic powers, progression, mana/fatigue, arbitrary scripting, a generic coordinate-targeting UI and automatic tactical decision policies are outside scope. Existing physical/scenario trees retain direct effect application; they do not need a fabricated ability or officer source.

## Existing-code inventory

The inventory describes the main checkout during planning. Directed-system implementation and unrelated mission/Console work are concurrent. Reconcile their delivered changes before editing; do not restore an older implementation to match this inventory.

| Existing area             | Current ownership                                                                                                                             | Migration responsibility                                                                                                                                                                        |
| ------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Power content             | `content/content.cppm`, `content/psychic.cpp`: entity-local `psychic_powers`, kind, energy, strength, recovery and ghost profiles             | Shared ability definitions and referenced native psychic effect profiles; final catalogue resolution, explicit overrides and export closure.                                                    |
| Personnel                 | `content/personnel.cpp`, `simulation/personnel.hpp`, `personnel_state.hpp`: officer `power`, duties, assignment, travel, health and detention | Ability grants and eligibility from native Personnel facts; recovery remains with the officer through reassignment. Replace single-power assumptions without changing current officer loadouts. |
| Apparatus and seats       | Choir definitions bind compartment, accepted powers, duties, Controllers and command channels                                                 | Explicit ability host/control/dependency bindings. Preserve seat capacity, compatibility, same-compartment and Station ownership rules for maintained psychic fittings.                         |
| Psychic execution         | `simulation/psychic.hpp`: preparation, target admission, demand, funded work and seat activation                                              | Move common activation, recovery, payment and interruption into shared ability execution. Keep native psychic simulation and reception.                                                         |
| Psychic state             | `simulation/psychic_state.hpp`: seat officer/target/activation/ready, ghost shapes and observer receptions                                    | One owner for each ability execution and native contribution; remove seat-owned officer cooldowns and duplicate active-state authorities.                                                       |
| Directed prerequisite     | Shared target qualification, native aiming and mode/effect interfaces delivered by slice four                                                 | Reuse permitted-contact and directed geometry interfaces for abilities requiring direction; do not create another target/aim solver.                                                            |
| Effects and resources     | Common prepared groups, stock transactions, receipts, shield/property lifetimes and periodic effects                                          | Shared ability costs and effect execution; temporary ability grant/removal through that same boundary.                                                                                          |
| Controls and observations | Psychic semantic controls/board, planning/execution channels, Personnel labels, Controller binding validation                                 | Standard ability controls and blockers through existing Console ownership and command admission. Preserve permitted psychic findings.                                                           |
| Continuation              | World initialisation/phases, psychic save/restore, native/WASM traces                                                                         | Persist grants, recovery, preparation, effects and native projection state with cross-reference validation and exact continuation.                                                              |

## Definition and ownership contract

### Ability definitions and native composition

Abilities are immutable catalogue templates with stable IDs, localisation, typed parameters and references to effects/native operations. Reuse pack dependency, inheritance, explicit override and Workshop export-closure rules. Compile once into typed data; no per-tick JSON evaluation or new C++ kind per authored ability.

Definitions declare applicable requirements, target policy, preparation work/timing, execution costs, activation/sustained effects, interruption response and recovery. A source binding supplies its concrete apparatus, payer, control endpoint and other named roles. Validate role compatibility at compilation where known and again at execution for dynamic bindings. No universal choir, battery, healthy officer or authenticated instigator is required by every ability; requirements belong to the ability and selected native operation.

Instant activation, prepared activation and sustained operation use one lifecycle. Reuse the preceding slices' typed conditions, targets, funding and effect groups. Add only the descriptors needed for the migrated native facts; this work is not a prerequisite to implementing the entire future expression language or custom-property catalogue. Source/target values and bounds must be explicit, checked and compatible with their operations.

### Source, grant, host and operator

Distinguish the performing source (officer/equipment), the grant that makes the ability available, any apparatus/seat it uses, and the Console/Controller authorised to operate it. These can be different objects. Source identity supports lifetime, recovery, stacking and diagnostics; it does not authenticate an effect.

A direct grant has a stable local identity under its source. A temporary grant additionally references its owning active effect and a stable local grant name. Refreshing/updating a grant preserves its execution and identity; it does not create another ability, reset recovery or pay startup costs again. Two independent grants retain separate lifetimes and can be removed independently.

One grant has at most one preparing or sustained execution at a time. Several grants can operate concurrently unless declared exclusion/dependency constraints prevent it. Effects already produced by earlier executions can remain independent after the ability itself returns to readiness. Use the existing original-source refresh rules for those effects; do not turn each activation into a new modifier source accidentally.

The ability's recovery owner is its performing officer/equipment, not its current seat or UI binding. Default recovery identity is that source plus the referenced ability; duplicate grants of the same ability must not bypass that recovery merely by presenting a fresh grant handle. Apparatus recovery is explicitly keyed to the apparatus. Define any distinct authored recovery keys during schema design, rather than inferring them from a runtime effect-instance ID.

Reassignment or loss of eligibility stops dependent work through its interruption contract and clears old execution/planning intent using the Controller's existing setting-reset path. It does not delete source recovery. A replacement officer uses their own readiness; a fresh grant or seat cannot erase the previous officer's deadline. Preserve recovery even while an officer is unassigned, travelling or temporarily unable to use the ability. Destroyed sources can be retired once no execution/independent-effect lineage requires their bookkeeping.

### Exclusions

Exclusion groups have an explicit owner/scope: a source or bound shared apparatus. Matching text labels on unrelated ships do not create a global lock. Preparing and active executions own their exclusions; paused executions retain them until stopped, so resuming cannot overlap an incompatible ability. Any separate group recovery is an explicit readiness constraint.

Validate exclusion availability, required costs and activation requirements together when committing a start. A rejected conflict changes no ability, spends no costs and starts no recovery. Simultaneous contenders resolve in documented stable command/execution order. Reject incompatible or cyclic apparatus bindings at compilation; do not wait indefinitely while holding a partial set of exclusions.

## Execution, preparation and costs

### Commands and lifecycle

Human players and Controller AI use the same admitted start/stop/target interfaces and permitted status. Discrete starts have stable request identities and outcomes; duplicate delivery or restore cannot start another execution. Sustained requested intent uses the existing persistent-setting model, including authored pause/resumption and the current psychic toggle behaviour. An unavailable one-shot request is reported as failed rather than silently retried every tick.

Choose and document exact lifecycle states and phase boundaries before implementation. They must distinguish unavailable, ready, preparing, active/paused and recovering without confusing requested intent with actual output. Activation effects execute only after preparation completes and current requirements/costs validate. Recovery is a deadline on its owner, not an active effect rebuilt by reenabling equipment.

Recovery timing is an authored contract: immediate abilities can begin it at committed activation; sustained powers can begin it when stopped, matching current psychic behaviour. Preparation interruption has an explicit cancellation/pause and recovery policy. Paid or completed work is not silently refunded. State transitions and cleanup must run once for manual stop, target change, dependency loss, grant removal and source loss.

Preparation binds its target/context at start and revalidates at activation. Changing that binding ends the old preparation under its interruption policy; progress cannot be transferred to an unrelated target. Sustained targeting follows its declared/native retarget contract. Maintained psychic target changes continue to stop the prior activation and apply recovery; switching target does not retain old ghost-activation identity as a shortcut.

### Payment and requirements

Name any costs at start, during preparation, at activation and during sustained work. Use shared store accounting and native funded grants; there is no second ability-energy balance. Costs paid in a completed preparation step are distinct from the final activation step. A later failure cannot refund earlier completed work, while a failed final required group cannot partially pay or apply that group.

Indivisible operations require full payment. Ongoing work declares proportional output, pause or stop under shortage. Preserve current psychic apparatus funding: request the sum of active seats' demands and apply the apparatus's funded fraction proportionally, with its existing heat/fault behaviour. Shared ability code must support that aggregate native grant without charging each seat a second time or creating a separate allocation priority.

Recheck actual officer eligibility, apparatus availability, targets, exclusion ownership and required stock at the relevant execution boundary. Do not reserve an entire indefinite stream of future energy at startup. Preserve real fractional work, rounding and paid progress through pauses and save/restore. Cost projection and effect-value projection must apply a property modifier at its defined boundary once.

Removing or expiring a temporary grant cancels pending preparation and stops dependent sustained work, releases its exclusions/unused reservations, and rejects stale queued requests for that grant generation. It does not reset source recovery, refund spent resources or remove independent effects already applied. Removing one grant does not erase an unrelated grant to the same source. Shared prepared transactions must validate temporary grants and their removal without exposing a partially applied required group.

## Targeting, control and visibility

Use existing typed entity/compartment/System/store targets and directed context where supported. The initial generic UI covers self and permitted contact selection plus native supported subtargets; no new free-coordinate or area-painting control is required. Range, geometry, relationship and apparatus qualification are authored/native constraints, not universal restrictions inferred from who granted an ability.

Target admission for humans and AI uses the controlling Console's permitted observations. Reuse the directed slice's contact lookup and dated/bearing context; adding an ability cannot reveal hidden state. Preserve psychic current eligibility: targetable known physical ships, ward on self or an observed ally, and last-permitted aim for directional reception. Native field reception may affect collateral receivers according to its actual geometry without publishing their hidden identities.

The ability execution layer is trusted physical execution. Scenario rules may use authoritative selectors and directly invoke supported effects as before. An effect's ability to inspect world truth does not publish that truth or create a control entitlement. Controller trees activate abilities through ordinary admission rather than calling unrestricted effect application.

Each controllable grant has an explicit control binding. Officer availability can follow a duty/seat into its authored Console; it is not automatically exposed to every Console that can see the officer. Temporary grants use an existing declared compatible control endpoint or remain unavailable for manual operation until bound. They cannot invent an unauthorised Controller writer. Stale controls/commands are rejected after reassignment, capture, replacement or grant expiry using existing ownership plus grant-generation checks.

Provide standard localised ability projections: source/grant label, permitted target, requested state, actual phase/output, preparation progress, costs, recovery and readiness blockers. Generic controls render these without a custom panel per effect. Keep planning vs execution bindings and complexity-profile delegation through ordinary Controller trees. Expose meaningful blocked reasons to the operator without leaking hidden enemy conditions. Enemy observation still follows sensing, reports and feeds.

## Psychic native-effect migration

Migrate cloak, scramble and ward profiles to referenced ability/effect definitions in the shared catalogue. Convert the officer's single `power` field into ability grants while preserving each maintained officer's current one-power fitting. Choir seats remain native apparatus/Personnel capacity and control bindings, not the universal owner of ability state.

Expose maintained psychic contributions through shared effect lifetime/operation interfaces. Cloak owns concealment strength, scramble owns projection activation and native ghost shapes, and ward owns suppression strength. Keep one authoritative owner for their native state; shared ability state points to its maintained contributions instead of copying shapes, receptions or active flags. Funding loss changes operation without recreating a contribution or assigning a fresh ghost activation on every resume.

Preserve emission-dependent concealment, native strength/winner rules and stable ties, actual coverage, friendly collateral, sensor capability requirements, ghost motion, observer-local identity/rejection/fade state, scan/weapon dispelling and scoped evidence. These native strongest-contribution rules are not comparable percentage modifiers to replace with additive sums. Removal of a projection ends its native reception/fading through existing semantics; retained evidence does not disappear merely because the source ability ended.

Maintain current health, detention, presence, travel, choir compatibility and shared-compartment requirements for production psychic abilities. Apparatus-free officer ability and equipment-only ability proofs use explicit synthetic bindings, not relaxed production choir rules. Keep Science/Support ownership, current planning/execution profiles and current tuning. The documented seat-to-officer recovery change is intentional and needs its own regression case.

## Authoring, persistence and integration

Workshop adds shared abilities, native psychic profile references, source grants, control/dependency/payer bindings, exclusions and preparation/recovery policy editing. Verify broken references, ambiguous control writers, incompatible apparatus, invalid grants, bounds and export closure. Shared-template mod overrides must affect consumers while explicit specialisations retain their differences.

Persist source-owned recovery even when no eligible control endpoint or grant is currently available. Persist direct/temporary grant identities and lifetimes, active executions, captured context, preparation work, exclusion owners, receipts and maintained-effect links. Native psychic saves retain ghost shapes, activation identity, receptions and rejection receipts. Do not restore funded work or winner caches as authority; rebuild derived values without advancing work, paying again or issuing new activation identities.

Validate recovery owners, grant lineage, effect links, exclusions, bound controls/apparatus, target references and finite deadlines against the compiled catalogue and actual restored objects. Reject orphan or conflicting executions. Choose a new save/schema boundary from the delivered prerequisite; incompatible older saves require a new session rather than a permanent legacy psychic execution path.

Audit existing effect transactions, scenario/tree entry points, resource demand preparation, Personnel reset/restore, capture/departure, Console models, network allowlists and rendering. Complete the migration by removing retired entity-local single-power authoring and common psychic activation/recovery/payment loops. Native psychic functions remain for physical behaviour. Record completion/evidence separately from this plan.

## Implementation sequence and gates

1. **Confirm prerequisites and compile shared ability definitions.** Reconcile the delivered directed/effect APIs. Specify role bindings, lifecycle, IDs, exclusion scopes, cost/interrupt/recovery policies and phase order. Gate: valid synthetic officer/equipment abilities resolve through catalogue overrides; invalid bindings and bounds fail compilation.
2. **Implement grants, source recovery and admission.** Add static source grants, explicit control bindings, start/stop receipts, concurrent ability ownership and atomic exclusions. Gate: human/AI requests share admission; conflicts have no side effects; officer reassignment neither clears nor transfers recovery; unrelated sources remain independent.
3. **Implement preparation and sustained execution.** Reuse shared transactions, work accounting, native grants, effect application and target qualification. Gate: instant, interrupted prepared and sustained synthetic abilities obey costs, cancellation, pause and recovery contracts; stale/duplicate requests cannot repeat work.
4. **Integrate temporary ability grants.** Add grant/refresh/removal as shared effect operations with lifecycle links and cleanup. Gate: expiry during preparation/active work, refresh, duplicate grants and regranting preserve recovery and independent consequences; removed grants cannot execute queued work or leave orphan exclusions/reservations.
5. **Migrate psychic powers and Personnel/apparatus content.** Move common execution into abilities and native outputs into maintained psychic contributions. Gate: all current cloak/scramble/ward contracts survive, including multi-seat proportional funding, source reassignment, loss/resume, target changes, ghost rejection/dispels, friendly reception and retained evidence.
6. **Complete controls, Workshop, content and continuation.** Migrate standard projections, semantic actions, delegation, catalogue/export, save validation and maintained definitions; remove superseded paths. Gate: an apparatus-free officer ability, equipment ability and temporary ability all use standard controls, with correct privacy and exact interrupted/prepared/sustained restore.
7. **Review and integration gate.** Review against this plan, audit all psychic/Personnel/effect/control call sites and resolve findings. Run affected native/WASM synthetic suites, matching continuation traces, content verification and required shared-loader/schema checks. Record any separately invoked GUI/accessibility/device smoke evidence under the [testing policy](testing.md).

Commit each verified logical step. Synthetic fixtures load through the real catalogue, admission and runtime without inheriting production ship/balance content. Do not freeze current ship tuning or require a test per function. A successful build or generic ability panel does not establish the migrated physical and knowledge contracts.

## Completion checklist

- [x] The delivered directed/resource/effect prerequisites are reused.
- [x] Officer and equipment grants share activated, prepared and sustained execution.
- [x] Explicit exclusions reject conflicts atomically; source recovery survives reassignment and grant churn.
- [x] Temporary effect grants have correct refresh, expiry, removal and independent-consequence lifetimes.
- [x] All existing psychic powers retain native behaviour with the intentional recovery-owner change.
- [x] Standard human/AI controls, Personnel, Workshop, maintained content and saves are migrated.
- [x] Retired common psychic execution paths are removed; review findings and verification evidence are recorded.
