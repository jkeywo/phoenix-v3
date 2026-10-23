# Shield migration — design/code review

23 October 2026 · static review against the working tree based on `319ada4`

Reviewed the [target design](gameplay-engine-design.md) against catalogue composition, shield execution, resource allocation, commands, observations, dependent gameplay, persistence and existing synthetic tests. Concurrent pack/menu changes were present during the review; they are outside this review's scope. No gameplay code was changed or runtime tests executed.

**Conclusion:** shields remain a reasonable first slice, but the design is not yet an implementation specification for that slice. The main gaps concern the lifecycle and bindings of a shield contribution that is no longer identical to a fitted System. They do not require designing the whole resource/recipe engine first.

Recommendations below remain proposals unless explicitly marked agreed.

## Decisions to settle before the first slice

### 1. Separate an installed contribution from an operational shield

**Existing behaviour.** [ShieldState](../src/simulation/combat.hpp#L67) lives for the lifetime of the fitted equipment. [Shield actuation](../src/simulation/combat_effects.hpp#L83) resets warmup when disabled or unpaid but retains strength. Disabled shields can recharge at their off-state rate, and can discharge into a battery. Initial strength is assigned when the equipment is constructed in [World](../src/simulation/world.cpp#L945).

**Design gap.** A maintained effect can stop when its source becomes unavailable, and removing a contribution removes its grant. The document does not say whether disabling the shield stops its operation, pauses its effect or removes its contribution. Remove-and-reapply could erase stored strength or recreate the initial reserve. A per-tick application that resets state would be equally incorrect.

**Recommendation.** Equipment installation creates a persistent contribution once. Enabled, funded, structurally available and discharging are operating states of that contribution, not reasons to recreate it. Preserve strength and fractional accounting through ordinary disablement/power loss. Removal/expiry is a distinct lifecycle event; subsequent genuinely new creation uses the authored initial value. Refresh updates the existing instance without refilling it. Explicit fill operations remain available when an author intends a refill.

**Agreed, 23 October 2026.** Equipment grants persist while installed. Enablement, funding and damage change operation rather than contribution existence. Lowering/power loss retains reserve, raising resumes warmup, and refresh does not refill. Removing equipment removes its contribution and remaining strength without refund. Temporary grants follow their own duration and dependencies; stopping coverage does not itself expire them. This contract is incorporated into the target design.

### 2. Define contribution granularity and identity separately from modifier stacking

**Existing behaviour.** One fitted shield has one arc, reserve, warmup and discharge state. Equipment ID identifies its state, commands and feed. [CombatSystemDefinition](../src/content/content.cppm) combines the geometry with the shield definition; [absorption](../src/simulation/combat_effects.hpp#L137) orders fitted shields by layer and equipment ID.

**Design gap.** A shield effect can contribute arcs, but it is not specified whether multiple arcs share a reserve. Also, the same-source/effect/target refresh rule is explicitly a timed-modifier rule. Applying it indiscriminately to capability grants could merge two deliberately distinct arcs from the same source/template on the same ship.

**Recommendation.** For the first slice, one named contribution owns one arc and its reserve. An effect can contain several named contributions. Identify each by its active-effect instance and a stable local contribution key; preserve original source separately for modifier refresh and diagnostics. Reevaluate/refresh existing contributions by identity. Do not infer capability-grant identity solely from the timed-modifier stacking key.

**Agreed, 23 October 2026.** Each named contribution owns one arc, an independent reserve and warmup state. Multi-arc effects contain several named contributions. Identity is the active-effect instance plus a stable local contribution name. Updating an existing contribution preserves its identity and state; separate instances remain independent even when they share a template. Original source identity remains separate for modifier stacking and diagnostics. This contract is incorporated into the target design.

### 3. Make funding and equipment dependency explicit bindings

**Existing behaviour.** The [compiler](../src/content/combat.cpp#L257) requires a power store and positive sustain/recharge conversion values for every fitted shield. [Resource bindings](../src/content/resource_bindings.cpp#L135) and [ShipResources](../src/simulation/gameplay_resources.hpp#L31) allocate request/grant slots per fitted System. [Actuation](../src/simulation/combat_effects.hpp#L83) reads that slot and returns discharged energy to that System's bound store. Faults and structural disablement are looked up by equipment ID.

**Design gap.** An objective or officer effect has no implicit fitted-System resource slot, compartment, discharge destination or repairable hardware. Merely putting its strength in a ship component does not make this path source-independent. The source's identity also cannot stand in for the target, payer and physical dependency when these differ.

**Recommendation.** The native shield contribution holds protection state. An equipment adapter binds its operating state, fault/structural dependency and existing power request to that contribution. The first non-equipment example uses an explicitly cost-free temporary grant with no discharge binding. Cost-free means no authored cost, not a privileged source class. A future paid non-equipment grant supplies an explicit payer/binding; no effect receives a fabricated equipment identity to satisfy the current allocator. Discharge requires an explicit destination and conversion contract.

**Agreed, 23 October 2026.** Costs and operating dependencies belong to the granting ability or process. Equipment grants use existing power allocation and equipment availability, with sustain taking priority over recharge. Other grants use their declared costs and dependencies, including none; there is no universal generator/battery requirement. Discharge requires an explicit destination and conversion rule. The first slice retains the equipment power adapter and demonstrates an explicitly cost-free temporary grant. Control of non-equipment contributions is addressed separately below.

### 4. Specify control and observation independently of the granting source

**Existing behaviour.** [ShieldDischarge](../src/simulation/combat_execution.hpp#L132) targets a fitted combat mechanism. [Own-ship shield projection](../src/simulation/combat_execution.hpp#L776) includes only shields whose equipment ID is in the Console's feeds. [Controller facts](../src/simulation/controller_actions.hpp#L201) identify the shield a Controller owns by matching its ID to physical consumers. [Console text](../src/core/console.cpp#L1753) uses that same ID.

**Design gap.** A scenario-granted arc has no equipment feed or physical command consumer. It can become invisible to the owning ship's UI/AI, or become accidentally controllable if it is assigned a convenient equipment ID. Granting source identity does not answer who can see, lower or discharge it.

**Recommendation.** Give the ship shield component an explicit observation/feed binding with stable contribution IDs. Retain equipment command channels, and bind them to the contributions they operate. Treat non-equipment contributions as observable through the permitted shield feed and read-only unless an operating binding is authored. Do not expose private source identity merely to label a visible shield. Human GUI and Controller AI use the same permitted projection.

**Agreed, 23 October 2026.** Contributions from all sources use the common shield observation interface through permitted feeds, with the same information available to human operators and Controller AI. Visibility does not grant control. Equipment retains existing command bindings; other grants expose controls only when authored. Remote sensing follows the normal evidence rules and need not reveal the granting source. This configures control and presentation without restricting trusted content's ability to apply effects.

### 5. Choose the shield property surface and a modifier transition

**Existing behaviour.** [shield_rate](../src/simulation/combat_effects.hpp#L55) uses the officer `recharge` slot. [personnel_scale](../src/simulation/personnel_state.hpp#L68) treats negative bonuses reciprocally rather than with the newly agreed additive-percentage rule. [fault_effectiveness](../src/simulation/combat_effects.hpp#L14) chooses the most severe fault and multiplies both recharge and usable absorption. Shield restore bounds also assume fixed capacity, warmup and conversion parameters.

**Design gap.** Retaining those calls unchanged would preserve a hidden effectiveness multiplier and old modifier arithmetic inside the first migrated system. Replacing the entire personnel/fault subsystem would make the slice much larger. The design also does not identify which shield parameters may change while a contribution holds reserve and fractional credit.

**Recommendation.** Define explicit shield properties and a narrow bridge from existing officer/fault facts. Express shield-facing modifiers through the new additive rule once, without applying the old multiplier again. Keep hard structural disablement as an operating condition. Preserve source facts and repair workflows outside shields. Document which property each legacy fault affects and how multiple faults combine under the new model.

For the smallest slice, freeze arc geometry, layer, reserve capacity, initial strength and energy-conversion parameters for a grant's lifetime, while supporting the chosen live operating-rate modifiers. If live capacity or conversion changes are required immediately, specify clamping, retained strength, fractional credit and restore bounds before implementation; those are observable accounting changes, not just field setters.

**Agreed, 23 October 2026.** The first slice supports live modifiers for recharge/off-state recharge rates, sustain energy cost on the granting process, discharge rate and available protection. Existing officer/fault effects feed those explicit properties using the additive rule once; structural disablement remains an operating condition. Capacity, initial strength, arc geometry, layer, warmup duration and energy-conversion values remain fixed per grant in the first slice. Later support for changing them requires explicit accounting rules. This scope is recorded in the migration notes rather than imposed as a permanent limit on the target design.

### 6. Define the first damage-hook contract at the mutation point

**Existing behaviour.** [penetrate](../src/simulation/combat_execution.hpp#L393) calls shield absorption immediately, then applies compartment damage. [combat_damage](../src/simulation/combat_execution.hpp#L66) performs casualties, faults and HP mutation together. Shield absorption changes reserves and emits visuals directly. [World tick](../src/simulation/world.cpp#L2605) orders actuation, motion, collisions and ordnance, but there is no generic pending-hit transaction. The [effect receipt helper](../src/simulation/effect_receipts.hpp) prevents duplicate tree execution; it is not a rollback mechanism.

**Design gap.** ECS order establishes broad sequencing, but a later phase cannot modify a hit that has already been absorbed. The design does not specify whether shield hooks run once per incoming hit or once per contribution, what a cancellation cancels, or which secondary operations belong to the same atomic step. An effect that damages another object from inside a pre-hook can introduce nested mutation before the original hit commits.

**Recommendation.** Introduce a typed pending shield-hit context at the existing absorption boundary and an actual-result record after resolution. Run pre-hooks before committing the affected mutation; run post reactions as separate executions afterward. Retain the existing physical phase order. Define hook scope explicitly; per-contribution hooks should identify the contribution and its assigned damage, while bypass remains a once-per-hit operation. Queue secondary damage for a documented later reaction stage rather than recursively reentering the active hit.

**Agreed, 23 October 2026 — hook scope.** Shield hooks run per affected contribution and only when damage reaches its shield layer. Earlier protection stopping the hit prevents hooks on later layers. Native geometry, layering and sharing determine presented damage; pre-hooks precede absorption and post-hooks receive its actual outcome. Context retains original-hit and contribution identities. This is incorporated into the target design.

**Agreed, 23 October 2026 — first-slice hook operations.** Pre-hooks can change or negate the damage assigned to that contribution. Negated damage is removed, not passed onward as bypass. Secondary effects execute as separate reactions after the native operation, with secondary damage queued for the documented reaction phase rather than recursively resolving inside the active hit. Reactions retain independent cost/failure semantics. This is a first-slice limit on the broader target design's pre-hook operation vocabulary. The [implementation plan](shield-migration-plan.md) carries the agreed contracts into staged work.

## Required integration work, not additional gameplay decisions

### Catalogue, authoring and loading

The current [Catalog](../src/content/content.cppm#L269) has tables for entities, maps, Consoles, trees, equipment, assemblies and messages, but no effect/property/ability/recipe table. Existing inheritance and override machinery is reusable. [Equipment instantiation](../src/content/equipment.cpp#L68) already supports templates with local merge-patch overrides and origin metadata.

Add the first effect definition kind through the whole path: pack resolution and validation, compilation, equipment references, [scenario dependency closure](../src/content/scenario.cpp#L451), [Workshop tables and previews](../src/content/workshop.cpp#L28), and relevant export/client-asset handling. A loader-only table would leave Test, authoring or dependency preparation incomplete. Preserve the distinction between authoritative definitions and the permitted presentation data sent to clients.

Choose a minimal schema containing reusable typed shield grants, stable local keys and the bindings above. A production physical/scenario apply/remove path must supply the second source used by the synthetic test. It must not be a test-only hidden-state mutation. This does not require all future abilities, recipes, custom properties or the full editor for every template kind in the first commit.

### All consumers must read the ship component

Moving absorption alone is insufficient. In addition to own-ship projection and controls:

- [Scanner shield findings](../src/simulation/sensors.hpp#L153) currently enumerate fitted shield mechanisms. They must see eligible contributions from any source while keeping acquisition and retained evidence on the detecting equipment.
- [Displacement/boarding blockers](../src/simulation/away.hpp#L351) enumerate enabled fitted shields. Leaving this unchanged would let displacement pass through a scenario-granted protective arc. Query the ship component through a native coverage interface.
- [Absorption and shield-hit visuals](../src/simulation/combat_effects.hpp#L137) must identify contributions without depending on a fitted shield child.
- [Actuation](../src/simulation/combat_execution.hpp#L352) and [projection](../src/simulation/combat_execution.hpp#L695) are currently guarded by CombatStructure, while resource actuation is driven by GameplayShip. Specify the first slice's supported host shape and ensure a compatible ship with no original fitted shield can receive a grant. Do not require a fake generator merely to join the query.

The first slice need not make every arbitrary world object shield-capable. Operation target compatibility is separate from source authority.

### Persistence is a new owner, not a renamed field

[save_combat/restore_combat](../src/simulation/combat_execution.hpp#L856) store state under the fixed fitted-System roster and reject mismatched counts/fields. A dynamic scenario contribution cannot live in that roster. Preserve its stable instance/source keys, expiry, reserve, warmup and fractional recharge/discharge credit in the new owner; restore must not replay initial grant application.

Both [World restore](../src/simulation/world.cpp#L3753) and [LocalSession restore](../src/session/local.cpp#L260) currently require schema 17. Update the schema gates together and preserve transactional restore. Old-save migration is already out of scope; do not reopen that decision.

### Equal layers need new code, but not a new shield-design interview

Current absorption is sequential even when layer values tie. Existing [layer tests](../tests/system/combat.cpp#L590) use different layers. The agreed even-sharing rule therefore needs a new synthetic case. Specify deterministic integer remainder handling and weak/depleted contribution handling in the implementation contract. The recommendation is to split evenly among eligible contributions, distribute integer residue by stable identity and redistribute unabsorbed shares among remaining capable peers before advancing to the next layer. This preserves the opportunity to use available protection without silently keeping the existing ID-first behaviour. It is a proposed detail for the agreed rule, not existing code behaviour.

Keep bypass applied once and preserve continuous-damage fractional carry. Do not route collisions through shields merely because compartment damage is being given hooks; [collision_damage](../src/simulation/combat_execution.hpp#L661) currently reaches compartment damage directly.

## Proportionate verification for the slice

Reuse the existing synthetic combat fixtures and production paths. Current coverage already includes layered bypass, off-state recharge, power-funded reserve behaviour, discharge conservation, fractional conversion and save continuation in [combat tests](../tests/system/combat.cpp#L590) and [fractional discharge tests](../tests/system/combat.cpp#L697).

Add a small set of scenarios that establish the new boundaries:

1. A shared effect template used by two independent sources, with an explicit mod override affecting inherited values and a local specialisation remaining intact.
2. Disable, power loss, refresh and removal as distinct transitions; no refill or reserve loss on ordinary enable cycles; removing one grant leaves the other intact.
3. A non-equipment grant participates in absorption, own-ship permitted projection, remote scan findings and displacement blocking without creating a fitted System.
4. An officer/fault bridge produces the agreed named-property result once, including a negative modifier that would differ under the old formula.
5. Pre-hook modification/cancellation precedes absorption; post-reaction payment failure leaves damage committed; ordering and secondary events are deterministic.
6. Equal-layer sharing, bypass-once behaviour and reserve accounting survive restore, including an expiring grant whose source is no longer present when its lifetime permits that.
7. Invalid catalogue references, missing required bindings and malformed dynamic continuation fail through the real loader/restore path without partial application.

Select cases appropriate to the agreed first-slice scope. Continue production content verification without balance goldens. GUI/accessibility/platform smoke evidence remains separate; this static review does not supply it.

## Recommended starting boundary

The six contracts above are now resolved. Implement only the catalogue/runtime support required by a native shield grant and its minimal apply/remove route, following the linked implementation plan. Preserve current resource allocation through an equipment adapter, migrate all shield readers, and prove an explicitly cost-free independent source. Carry unrelated systems through their existing paths until their own migration slices.

The resources/recipes overhaul, paid arbitrary-source consumers, full generic expression vocabulary and all compartment/System hooks need not precede this slice. Their interfaces must remain compatible with the target design, and unsupported first-slice cases must be rejected explicitly rather than appearing to work.
