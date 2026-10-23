# Shared property effects and Duty Officer migration

23 October 2026 · implemented; automated evidence and remaining acceptance limits are recorded in the [implementation record](property-effects-evidence.md).

This is the second slice of the [shared gameplay engine](gameplay-engine-design.md), following the [shield migration](shield-migration-evidence.md). The selected scope is shared property effects and all existing Duty Officer bonuses, including adapters for existing faults and boost/impulse modifiers where they affect the same properties. The [migration notes](gameplay-engine-migration.md) retain the wider conversion sequence.

## Intended result

An officer trait, a temporary scenario effect and an existing equipment process can contribute to the same named property through one mechanism. Removing one contribution leaves the others intact. Equipment continues to own movement, aiming, sensing, firing, repair and other physical work; the property mechanism supplies their effective values.

For example, an engineer supplies +25% shield recharge, a scenario supplies a temporary +25%, and an output fault supplies -50%. Together they leave recharge at its base rate. Relieving the engineer removes only that officer's contribution. Expiring the scenario effect removes only its contribution. None of these transitions refills the shield or resets its warmup.

This stage completes the ordinary officer-buff conversion, including the less visible repair, Medical, security and away-work paths. It does not convert psychic powers merely because an officer owns them.

## Scope

Include:

- Typed native property descriptors and reusable modifier effects in the existing mod-extensible effects catalogue.
- Common application, refresh, removal and lifetime bookkeeping for modifiers and existing shield grants, while leaving shield reserve state in the shield component.
- Maintained officer contributions driven by existing duty, travel, health and deployment facts.
- Timed scenario modifiers and modifier applications from the existing deferred shield-reaction path.
- Existing fault and boost/impulse adapters on migrated properties, with their native operating and funding mechanisms retained.
- All dependent observations, Personnel presentation, Controller assignment preferences, Workshop authoring, content migration and continuation.

Defer resource/store/recipe unification, general custom properties, arbitrary expressions, general trigger/ability authoring, psychic-power migration, weapon payload effects, directed-system unification and additional damage-hook families. Native property registration is the extension boundary for this slice; declaring an arbitrary custom property remains later work.

## Existing code and required changes

The code inspection found several distinct paths that must converge. Paths below identify implementation seams, not a fixed file layout.

| Current path                                                                   | Migration responsibility                                                                                                                                                          |
| ------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/content/personnel.cpp`, `content.cppm`                                    | Trait bonus maps, duty-to-System slot bindings and pre-aggregated officer bonuses become compiled effect references and compatible target bindings.                               |
| `src/simulation/personnel_state.hpp`, `personnel.hpp`                          | Replace the derived bonus map and `personnel_scale`/`officer_value` calculation with maintained contributions. Preserve roster, assignments, travel, eligibility and authority.   |
| `combat_effects.hpp`, `combat.hpp`, `shield_reactions.hpp`                     | Reuse shield grant identities, prepared execution, source bookkeeping and deferred reactions. Replace shield-only modifier arithmetic with the shared property calculation.       |
| `world.cpp`, `combat_weapons.hpp`, `combat_execution.hpp`, `conversations.hpp` | Replace officer multiplication and comparable fault/boost multiplication at physical consumers. Keep actual funding, weapon operation, mount motion and radio publication native. |
| `teams.hpp`, `boarding.hpp`, `away.hpp`                                        | Remove direct trait-bonus reads and special officer scaling in clinical, boarding and deployed work. Preserve team participation and paid work commitments.                       |
| Personnel/core projections and Workshop editors                                | Show applicable contributions and effective values from the same compiled/runtime model. Update assignment scoring, summaries, authoring and export closure.                      |

The shield implementation has already been simplified to share its grant constructor and compiled contribution definitions. Build on that work; do not reintroduce copied combat-system definitions or a second grant implementation. Reinspect the working tree before implementation because adjacent personnel and shield work is active.

## Property coverage

The first compiler stage must produce a checked inventory of every current consumer. These are the required semantic properties; exact serialized IDs are frozen with that stage.

| Existing bonus or adapter | Native property coverage                                                                                                                                   |
| ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `propulsion`              | Engine acceleration, including its demand/actuation calculations.                                                                                          |
| `sensors`                 | The equipment-local range values currently reached by the officer bonus; include ordinary, long-range and directed acquisition consumers where applicable. |
| `cycling`                 | Weapon cycle rate. Projectile damage and payloads remain separate.                                                                                         |
| `traverse`                | Turret traverse rate.                                                                                                                                      |
| `reload`                  | Magazine replenishment work rate; do not silently apply it to weapon loading or logistics delivery timers.                                                 |
| `recharge`                | Shield recharge and off-state recharge rates.                                                                                                              |
| Existing shield modifiers | Sustain cost, discharge rate and available protection, retaining first-slice limits.                                                                       |
| `repair`                  | Component/structural repair work and the existing standalone/deployed specialist-work contribution.                                                        |
| `medical`                 | Treatment and stabilisation work rates.                                                                                                                    |
| `security`                | The security-team contribution used by native boarding resolution.                                                                                         |
| `comms`                   | Radio range.                                                                                                                                               |
| Boost/impulse targets     | The existing speed, acceleration and turn-rate properties targeted by these processes.                                                                     |

A property descriptor declares its owner kind, unit, base-value source, bounds, supported percentage modification and evaluation boundary. There is no universal effectiveness property. A property does not become writable merely because an implementation struct has a numeric field.

Shield capacity, current strength, geometry, layers, warmup duration and conversion values remain outside live modification. Stored resource quantities, ammunition, health and work already completed remain native state, not percentage properties.

## Composition and operation

For a migrated property, sum signed percentage deltas against the unmodified base and apply them once:

```text
effective = clamp_for_property(base × (1000 + sum(delta_permille)) / 1000)
```

Use checked fixed-point integer arithmetic and explicit remainders where progress requires them. Validate definition magnitudes and aggregate limits before multiplication. Preserve native/WASM determinism and a zero base. Do not feed a previous effective value back in as the next base.

This deliberately replaces reciprocal negative officer scaling and strongest-only comparable modifiers on the migrated properties. Negative totals can reduce a nonnegative rate to zero. Zero progress pauses the relevant work without erasing progress or spending a new indivisible work cost. Positive modifiers cannot resurrect destroyed/disabled equipment, bypass missing supplies, remove thermal stops or grant command authority.

Rates and durations must not be confused. A faster cycle rate produces a shorter interval; it is not a positive duration bonus. Keep a minimum one tick for a permitted positive-rate discrete operation. A zero rate blocks a new operation instead of dividing by zero or granting a one-tick cycle. Preserve already committed native cooldowns unless that mechanism already supports live adjustment. For ongoing work, retain completed work and fractional progress across changes; freeze exact rounding and timer behavior in the consumer contract before its migration.

Fault adapters map each applicable output penalty to the explicit affected properties. Multiple comparable penalties join the additive sum. Destruction, disabled/feed faults and structural availability remain hard operating conditions. The old `fault_effectiveness` path may remain for unmigrated mechanisms, but must not scale a migrated property a second time.

Boost/impulse processes retain their payment, charging, interruption, restrictions and controls. Their funded output supplies maintained property deltas; full funding supplies the declared delta and partial funding scales that delta under the existing process contract. Do not charge the modifier again. Preserve the existing rule for which processes may operate simultaneously; when several eligible contributions exist, combine them additively.

Actual supply fraction and native participation rules are distinct from a comparable property bonus. Document their position in each consumer calculation. Audit team injury output penalties as well: translate comparable penalties on migrated work into explicit contributions, while retaining eligibility and the native multi-team participation rule. No undocumented multiplication may remain between two comparable modifiers of the same property.

## Effect definitions, targets and identity

Extend the existing effects catalogue with typed modifier contributions. A reusable definition names the property, signed delta and a compatible target role. Traits reference these definitions; duties bind roles to concrete equipment, shield contributions or supported work participants. Compile bindings once. An officer may have no applicable effects for a duty, as today; unknown properties, invalid bindings and partially bound required effect groups are errors.

Keep trait IDs used for qualifications and psychic seats. Removing the old bonus representation must not remove those identities. Use shared effect references and inheritance so mods can change a bonus across several traits or ships without editing copies. The initial modifier schema needs fixed typed deltas and bounded application values, not an expression language.

Separate four identities: original source, effect definition, active application and named contribution. A target is a stable logical entity/component address, not a Flecs allocation or vector position. Officer sources retain ship, officer and trait/application identity; two officers using the same template remain independent. Repeated duty bindings must not duplicate a contribution to the same target/property.

An officer's maintained application follows actual eligibility. Reassignment removes the old targets immediately; new targets become eligible on arrival. Injury, detention, death, recovery, team movement and away deployment use their native facts. Deployed work must bind to its actual participant/work context, including paths which currently read traits directly. It must neither keep a stale shipboard bonus nor count one specialist twice.

Timed modifier reapplication from the same original source, definition and target refreshes the matching named contributions without accumulating copies or shortening expiry. Different sources add. Explicit application handles support selective removal. Original source disappearance does not cancel an independent timed application; an officer-maintained application still stops when its declared dependency fails. Existing shield grants keep their separate intentional-instance semantics and must never be merged by modifier refresh rules.

Generalize the shared application service only as far as these two contribution kinds require. Common metadata owns lifecycle and receipts; native components own actual shield state and effective-property projections. Avoid parallel officer/scenario modifier stores or per-tick JSON interpretation. Removing a property contribution changes future effective values and never reverses completed physical consequences.

## Execution, observation and persistence

Reconcile due expiries and changed maintained sources at explicit ECS boundaries. The authoritative effective values used for demand quotation and the operation they fund must agree. An adapter must not compute an expensive output from new modifiers after quoting its cost from old ones. Preserve immediate native availability checks where damage can disable equipment later in the tick.

Scenario applications use ordinary production state-tree execution and receipts. Trusted authored content needs a valid supported target, not a player identity. Controllers continue to submit admitted commands and see only permitted observations. A failed required multi-target application commits neither partial contributions nor payment. Existing shield reactions can apply/remove a modifier effect using the same prepared execution and next-tick schedule; this does not introduce a second event scheduler.

The acceptance example combines an officer-maintained modifier, a timed scenario modifier and a fault on one shield, then demonstrates the same modifier mechanism on a sensor or engine. Add one deferred shield reaction that applies a timed modifier to a bound equipment target. Removing, expiring and refreshing these contributions must preserve unrelated shield and equipment state.

Personnel shows the officer's applicable contributions; normal mechanism views show effective values through their existing permitted feeds. Keep source explanations scoped: a visible effective value does not reveal a secret officer or scenario source. Human and AI views use the same permitted data. Preserve assignment suggestions and Controller policy scoring using compiled trait effects rather than obsolete bonus maps. Provide localised, stable semantic identities for changed rows and actions.

Persist independent applications, source keys, targets, captured values, expiries, stable identities and pending execution receipts. Derived aggregate caches are rebuilt. Maintained-source reconstruction must reconcile rather than add duplicate saved contributions. Preserve shield reserves, paid work and fractional progress. Restore validates a complete replacement before changing the live world, including unknown properties, incompatible targets, duplicate contributions and inconsistent dependency state. Increment the current world/session schema when necessary; determine its number at implementation time and reject unsupported old saves consistently.

## Implementation sequence

The stages below retain the implementation and acceptance contract. Delivery and verification results are recorded in the [implementation record](property-effects-evidence.md).

### 1. Freeze the property and authoring contracts

Complete the consumer inventory above, including hidden trait reads, restore validators and UI/assignment calculations. Define native property descriptors, compatible target roles, modifier-effect schema, bounds, rounding and rate/timer behavior. Add catalogue compilation, reference/inheritance/override validation and dependency closure. Include Workshop support as the schema becomes authorable.

**Gate:** synthetic content resolves shared effects and mod overrides, rejects incompatible bindings and unsupported fields, and survives draft/export/reload without losing references. Every existing officer slot has an identified consumer and migration destination.

### 2. Deliver one complete modifier path

Add shared modifier application/lifetime support and integrate shield recharge as the first physical consumer. Use production scenario actions to apply, refresh and remove a timed modifier. Reuse the established shield component, prepared execution and observation boundaries.

**Gate:** two independent sources combine; same-source refresh does not stack or shorten duration; selective removal and expiry preserve shield strength/warmup; failed application is atomic; save/restore continues identically. No new officer-specific calculation is introduced.

### 3. Convert officer sources and shield consumers

Traits reference shared effects; duties bind targets. Reconcile maintained contributions from assignment, travel and health facts. Convert all supported shield properties and their fault bridge. Update Personnel summaries, scoring and authoring alongside this path.

**Gate:** assignment, relief, swap, travel, injury/recovery and death update only the relevant contributions; officer, scenario and fault deltas combine once; positive bonuses cannot defeat hard disablement; dynamic properties do not change shield reserve identity or cost accounting.

### 4. Convert equipment properties and source adapters

Migrate propulsion, sensing, turret traversal, weapon cycle, magazine replenishment and Comms range, together with applicable fault and boost/impulse adapters. Preserve sensing ownership, native flight and funded operation. Remove old officer arithmetic at each migrated consumer.

**Gate:** representative synthetic paths prove actual movement, acquisition, traversal, firing/replenishment and radio delivery; unfunded/disabled operation stays blocked; boost/officer/fault deltas combine once; zero rates and resumption preserve the declared progress/timer contract.

### 5. Convert team and deployed work

Migrate repair, Medical, security and away specialist contributions, including direct officer trait reads and native output penalties. Preserve participation, qualification, transport, paid material work and task progress. Keep psychic qualifications intact.

**Gate:** on-ship and deployed contributions follow the actual eligible officer/team; no double application; zero work cannot pay and perform a new batch; interrupted paid work resumes without repayment; treatment, repair and boarding retain authority and continuation behavior.

### 6. Integrate reactions and remove superseded paths

Allow existing deferred reactions to apply/remove the supported modifier effects with explicit compatible target bindings. Migrate maintained production/development content, all projection clients and fixture authoring. Remove old bonus aggregation/scaling and obsolete modifier branches for migrated properties. Update save validators, content docs, officer documentation and delivery records.

**Gate:** a real shield hit schedules a modifier application; success/failure, captured source identity, disappearance of the original source and pending save/restore behave correctly. Catalogue verification and Workshop export/reload pass. Searches find no live legacy officer scaling or direct trait-bonus consumers.

### 7. Review and integration acceptance

Use an independent agent review to check scope coverage, duplicate paths, payment/availability order, source identity and authority leaks. Fix findings and run affected checks, then the current complete native and WebAssembly gates. Extend an existing synthetic trace to compare active timed modifiers, maintained officer transitions and pending reaction application across native/WASM continuation.

Record actual test results and retry/failure limits. Graphical Personnel/mechanism/Workshop journeys and physical accessibility/device/performance checks remain separately triggered under [testing policy](testing.md). Do not claim those from headless integration.

## Validation boundaries

Use synthetic production-loader fixtures independent of shipped ships and tuning. Extend existing content, personnel, combat, flight, sensors, Medical, security, away, conversations, scenario World and Controller suites where they own the behavior. Test shared modifier contracts centrally and representative native integrations at their boundaries; do not add a test per helper or production-content goldens.

Required cross-cutting cases are additive positive/negative composition, zero-base and zero-rate behavior, checked bounds, same-source refresh, independent sources, selective removal, source loss, actual duty/deployment eligibility, no double payment, no duplicate restore, scoped observations and exact native/WASM continuation. Preserve the shield migration's existing regression coverage.

## Completion

- [x] All ordinary officer bonuses use reusable shared effects and typed property bindings.
- [x] Every migrated property has one combination path, including applicable native-source adapters.
- [x] Independent scenario and shield-reaction applications use the same lifecycle and prepared execution.
- [x] Physical consumers, Personnel/Controller views, Workshop and maintained content are migrated together.
- [x] Old officer scaling/direct bonus reads are removed; remaining unmigrated mechanisms are documented.
- [x] Persistence, native/WASM parity and independent review have recorded evidence.
- [x] Manual acceptance and remaining wider-framework work are recorded separately.

The recommended following stage is resource/store unification, then directed systems and weapon payload effects. This plan does not establish implementation or approval of those later stages.
