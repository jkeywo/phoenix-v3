# Directed systems and payload effects migration

23 October 2026 · implementation plan; implementation and acceptance are pending.

This is the fourth slice of the [shared gameplay engine](gameplay-engine-design.md), following [resource, store and recipe unification](resource-store-migration-plan.md). The resource slice is a prerequisite, not completed work. Recheck its delivered interfaces and the live combat/sensor paths before implementation.

## Intended result

Weapons and focused scanners use a shared directed capability for target selection, operating modes, qualification, funding and effect execution. Native mechanisms retain turret motion, projectile flight, collision, penetration, beam contact and equipment-local sensing. A mode composes supported operations; adding another authored gun, scan mode or payload does not require another equipment-specific execution loop.

An equipment assembly can contain several directed capabilities. Each capability has one active operating mode and one targeting mode, selected separately. It declares the combinations it supports. A fitting can offer scanning and firing as alternative operating modes; simultaneous scanning and firing require separate directed capabilities. Target and magazine selections remain separate from these mode selections.

## Settled scope

- Convert all existing projectile weapons, beams, point defence and focused scanners, including their use by carried craft and autonomous ships.
- Keep psychic powers for the later ability migration. Preserve existing psychic interactions with weapon exposure, apparent contacts and scan/weapon dispelling.
- Separate targeting modes from operating modes. Preserve designated-contact targeting and automatic missile interception, with explicit compatibility rules.
- Make operating-mode selection immediate. Stop the old sustained operation, preserve readiness/progress, and leave launched projectiles and inflicted effects independent.
- Support explicit payload bindings to shield-hit and compartment/System-hit outcomes. Native damage resolution determines which outcomes occur.
- Include periodic active effects, proved by a synthetic burning-ammunition example. Fire spreading, firefighting gameplay and new production ammunition balance are outside this slice.
- Convert both impact and destruction payloads, including ship explosions. Keep their distinct authored profiles and existing physical rules.
- Include common catalogue definitions, mod overrides, Workshop, human/Controller controls, observations, maintained content, persistence and deterministic native/WASM continuation.

Broad radar detection remains equipment-local native sensing; it is not turned into a repeated directed shot. New guidance algorithms, post-launch retargeting, turret arbitration, target-selection AI, physical damage redesign and general psychic/region/ability conversion are outside scope.

## Existing-code inventory

This inventory describes the checkout used for planning, before the resource migration. Several combat and content files have concurrent work; reconcile it when implementing rather than restoring an older snapshot.

| Area | Current path and responsibility | Migration responsibility |
| --- | --- | --- |
| Weapon definitions/state | `content/content.cppm`, `content/combat.cpp`, `simulation/combat.hpp`: weapon families, magazines, load/cycle state, beam fractions, point-defence mode | Compile shared directed definitions and mode references; retain native mechanism state with one owner. |
| Targeting and launch | `simulation/combat_weapons.hpp`: permitted contact lookup, lead, mount geometry, interception, loading and launch | Shared targeting/qualification and funded operation; keep contact visibility and physical aiming. |
| Turrets | Combat mechanisms and weapon/sensor aiming passes | Shared directed aim interface; preserve mount traversal, geometry, faults and single targeting ownership. |
| Focused acquisition | `content/sensors.cpp`, `simulation/sensors.hpp` and sensor state | Invoke native acquisition through directed operation effects, retaining exposure, snapshots, discovery receipts and publication on the sensing equipment. Verify exact file boundaries before editing. |
| Damage and shielding | `simulation/combat_execution.hpp`, `combat_effects.hpp`: penetration, compartment damage, faults, shield absorption and reactions | Typed hit outcomes, shared damage operations and payload bindings without duplicating the native damage calculation. |
| Ordnance and destruction | `simulation/combat_ordnance.hpp`: beams, collision, proximity, blast exposure, cover and once-only destruction | Carry committed payloads; route impact and destruction through shared effect execution, preserving separate profiles. |
| Existing effects | `simulation/effect_application.hpp`, property/shield effect state and deferred reactions | Extend common preparation, identity and lifetime handling for immediate operations, payload applications and periodic reactions. |
| Resource prerequisite | Shared stores, loaded-resource commitments, native grants and resource ammunition profiles from slice three | Use delivered accounting and the actually loaded resource; no parallel ammunition or energy payment path. |
| Integration | Controller commands, Console models, rendering, Workshop, serializers, scenario effects and tests | Replace old family-specific execution paths and retain supported controls, knowledge boundaries and continuation. |

## Shared directed contract

### Definitions, composition and modes

Use reusable catalogue definitions/references for directed modes and effect groups, with the existing inheritance, explicit override, localisation and export-closure rules. Compile typed operations and bindings once; no runtime JSON interpretation or C++ enum entry for each authored mode or payload.

A directed capability binds its physical origin, optional turret, command inputs, supported target policies, operating modes, resource inputs and native operation state. An operating mode declares applicable qualification, work/cycle timing, immediate and ongoing costs, and effect execution points. The implementation should expose native primitives for launch, beam work and scan acquisition through the shared execution boundary, rather than reducing the migration to a common UI over separate old schedulers.

Preserve existing discrete/repeating weapon operation and sustained beam/scanner work. Do not add a burst editor or general workflow language merely to represent these forms. Compatibility validation rejects invalid mode/target-policy/resource combinations. Ordinary installed capabilities use the existing equipment lifetime; dynamically granting directed capabilities is deferred to the later capability/ability work.

An assembly needing concurrent directed operations uses separately identified capabilities and explicit input bindings. A physical turret retains one targeting owner in this slice. Concurrent capabilities may use distinct mounts or supported fixed geometry; sharing a mount does not introduce last-writer-wins aiming.

### Targeting and knowledge

Controllers continue to choose intent through the common admitted command interface. Directed machinery executes the chosen policy. Preserve the existing bounded missile-interception policy as native automation; do not attach a new decision tree to each weapon or scanner.

Share permitted contact resolution and aiming inputs across weapons and scanners. Retain Console-scoped observations, apparent contacts, bearing-only returns and dated last-known positions. Target eligibility must not reveal hidden range, motion, identity or ghost provenance. Bearing-only fire remains possible with its existing physical reach and seeker rules. Projectile collision and seekers use their existing authoritative physical rules after launch; they do not grant observations to the firing Console.

Weapon range, target policy, physical coverage and payload application are defined by the using mode/mechanism. Resource-attached effects do not select targets or execute merely because ammunition is loaded, moved or consumed. The mode explicitly chooses which resource payload to carry or invoke. Preserve the resource slice's native ammunition profile and existing projectile/loading/cycle defaults while removing superseded inline ownership.

Detection, unfinished acquisition, findings and publication remain on the sensing equipment. A directed acquisition operation advances supported capabilities through the current funded/covered native scan path; it cannot directly reveal arbitrary target fields. Broad detection and focused acquisition on the same equipment keep their distinct qualification rules. Preserve per-target/per-capability exposure, discovery-once receipts and retained observation timestamps. Different modes may resume compatible acquisition; unrelated acquisition definitions must not share progress accidentally.

### Switching, readiness and costs

Admit a mode change through the ordinary Controller setting path and apply it at the next normal physical input boundary. It is not queued until a beam, scan or firing cycle completes. Stop old sustained work before authorising work for the new mode.

Retain actual loaded ammunition, unload-return commitments, load/unload work, cooldown deadlines and native scan progress. The new mode cannot become ready by resetting or evading these obligations. Incompatible loaded ammunition follows normal unloading/loading before use; selecting a mode does not rewrite a loaded payload or change a magazine's selected resource. Equipment enablement, faults and power loss remain operating conditions.

An indivisible launch secures all required costs and validates its required operations before committing once. Sustained work uses native funded grants and fractional accounting; scanners retain full-power acquisition qualification, while existing beam scaling and standby rules remain intact. Apply existing property modifiers once, at the defined demand/work boundary. Cancellation stops future payment and work without refunding completed work.

Changing mode or target, losing power, removing the equipment or destroying the source cannot retract a launched projectile or remove an independent inflicted condition. A launched operation carries its committed resource/payload, evaluated source values and stable original-source identity. Later collision context and target state are evaluated when the hit occurs. A sustained operation evaluates current funded inputs for its next work step.

## Payload and hit contract

### Native delivery and explicit bindings

Launch, collision/contact, shield resolution, penetration and destruction are distinct execution points. An authored payload binds to supported typed outcomes and declares its target from that context. The selected contact is not automatically the object actually hit.

Shield outcomes identify the affected contribution and original hit; they occur only for contributions whose layer is reached. Preserve equal-layer sharing and current shield pre/post semantics. Compartment outcomes identify the actual struck compartment and resulting damage. System outcomes occur only when the native path identifies an affected System; do not invent a universal system hit from a ship target. Implement explicit publication from the native damage/fault path where it currently lacks that result.

Bindings can require an outcome such as damage reaching a compartment or positive inflicted damage. Zero damage, full absorption and cancellation remain distinguishable. Multiple reached contributions or compartments can produce distinct outcomes; execution identity must distinguish them while preventing duplicate processing of the same outcome. A payload declaring several bindings deliberately receives those matching outcomes.

Use post-resolution outcomes for secondary payload applications in this slice. Keep existing shield pre-damage modification/negation support; general arbitrary pre-damage effects are not required by the new hit bindings. Secondary damage enters a scheduled phase, never recursive resolution inside the current hit.

The mode's damage operation still uses native shielding, penetration, deposition, faults and destruction. A later payload reaction validates its own targets and costs atomically. Its failure does not undo a committed launch, impact or earlier damage. Reuse the existing prepared-effect boundary for required groups; do not create weapon-only effect transaction semantics. Trusted scenario effects may invoke the same supported operations without fabricating an authenticated weapon source.

### Blast and destruction paths

Keep area selection, distance/falloff, solid cover, altitude, collateral and physical contact in native delivery. Apply the shared damage/effect path to each qualifying target using explicit hit context. Include ordinary destructibles and interceptable ordnance, not just ships with compartments. Effects requiring a shield, compartment or System reject incompatible bindings at compilation where known, and validate runtime targets before application.

Impact/proximity warheads and destruction explosions remain separately authored. Shooting down a missile must not silently invoke its impact payload or inherit its shield bypass. Preserve existing missile destruction restrictions, deterministic chance, visual/audio timing and once-only destruction-chain limits. A missed projectile does not apply a selected-target payload; expiry does not become an explosion unless an existing/authored supported operation calls for one.

Shared dispatch must not retain damage-positive early exits that accidentally suppress an explicitly valid non-damaging payload. Native projectile survival/penetration and blast delivery need explicit qualifications so a visual-only or effect-only definition is handled deliberately, without inventing physical penetration for it.

### Periodic active effects

Add typed periodic reactions to active effects using the shared source, target, lifetime and removal contracts. The initial example applies a timed Burning condition to a compartment after qualifying penetration. It performs periodic native damage to that compartment; damage to an internal compartment is not a new external projectile that traverses the ship's shields again.

The same original gun applying the same condition to the same compartment refreshes its duration without shortening it; other sources remain independent. Include definition/binding identity in the refresh key so unrelated effects do not merge. Refresh preserves periodic cadence and cannot manufacture an immediate extra tick or postpone all damage indefinitely. Values follow the existing applied-value/refresh contract. Capture the condition's parameters when applied/refreshed; do not read a destroyed gun for later work.

Expiry or explicit removal stops future reactions without reversing damage. Target removal retires the condition safely. Removal before a queued reaction commits prevents that removed instance from acting; use the instance identity/generation to reject stale work. Pause/funding dependencies are explicit rather than inferred from source existence. The Burning proof is independently lasting and has no ongoing gun-energy requirement.

Choose and document exact phase ordering, first-period scheduling and expiry-boundary behaviour before implementing the first fixture. Use positive bounded periods, checked arithmetic, deterministic ordering and bounded pending work. Preserve deadlines and receipts in saves; restore must neither replay completed ticks nor skip a due tick. Broader spreading rules and extinguishing gameplay are deferred; explicit effect removal proves termination.

## Presentation, authoring and persistence

Provide stable standard projections for operating/targeting mode choices, requested target, readiness/blockers, funding and native progress. Human players and AI use the same admitted commands and permitted facts. Scanners retain their existing findings/readout; weapon controls retain magazine/loading information. UI code uses capability metadata and standard native interfaces rather than requiring custom setup for every authored effect.

Update Controller channel validation, semantic controls, localisation, radar overlays, point-defence controls, resource telemetry, Workshop editing/preview and export dependency closure. Audit direct references to weapon family, numeric point-defence modes and scanner-only target commands. Preserve reported weapon emission for cloak and hostility/objective events attached to actual attacks. Sharing internals must not disclose hidden effects or faults to an enemy observer.

Persist selected modes, intent, readiness, loading commitments, beam fractions, scanner exposure/evidence, in-flight captured payloads, effect/source identities, periodic deadlines and pending execution receipts. Validate all saved references, bounds and cross-owner bindings. Choose the next save schema from the implementation checkout; require a new session for incompatible older saves instead of retaining a second runtime.

Migrate maintained content and synthetic fixtures through the new catalogue. Shared definitions should demonstrate mod-wide overrides while explicit specialisations retain their differences. Do not change shipped costs, ranges, scan durations or ammunition balance as incidental migration work. Remove retired authoring and execution paths after their replacements work.

## Implementation sequence and gates

1. **Confirm the prerequisite and compile the shared model.** Verify the resource migration's actual store, loaded-round, grant and ammunition-profile contracts. Specify typed mode, delivery, outcome and effect bindings, phase ordering and validation bounds. Add catalogue/Workshop dependency handling and a synthetic directed fitting. Gate: compilation rejects incompatible bindings and resolves shared overrides deterministically without touching runtime balance.
2. **Migrate focused scanning as the first operating path.** Introduce shared target/aim/readiness state and mode commands; route funded scan work through its operation effect while preserving native acquisition and knowledge. Gate: target changes, bearing-only contacts, ghosts, power/coverage interruptions, mode switches and restored progress obey the existing sensor contracts. This establishes the non-damaging use of the core first.
3. **Migrate projectile launch, magazines and interception.** Use the same directed core, shared resource commitments and native flight. Convert targeting modes, launch effects and captured payload context. Gate: launch/payment is once-only; loading/unloading and switching conserve stock; source loss does not alter in-flight work; interception and designated targeting use permitted contacts.
4. **Unify hit outcomes, beams and blast/destruction payloads.** Route direct, penetrating, sustained and area damage through native resolution with explicit outcomes and shared secondary execution. Gate: shield layer reach, compartment/System bindings, actual collateral targets, fractional beam damage, independent reaction failure and distinct warhead/destruction behaviour are proven together.
5. **Add periodic conditions through the common effect lifecycle.** Implement periodic scheduling, refresh, removal and continuation, plus the synthetic Burning payload. Gate: refresh cannot duplicate/starve ticks; distinct sources remain independent; source removal preserves the condition; target removal/expiry/cancellation stop future work; secondary damage cannot recurse unboundedly.
6. **Complete authoring, controls, content and persistence.** Migrate maintained definitions, Controllers, Console projections, Workshop/export and save encoding. Remove obsolete execution paths. Gate: production verification and representative synthetic human/AI command journeys succeed, including a scan/fire multimode fitting and two concurrent independently mounted capabilities.
7. **Review and integration gate.** Review the implementation against this plan, re-audit every damage/launch/acquisition call site and fix identified gaps. Run affected native/WASM system suites and matching continuation traces, then the required shared-schema/content integration checks. Record implementation evidence separately from this plan; make no unrun manual acceptance claim.

Commit each verified logical step. Intermediate steps may use temporary adapters within the branch, but the completed migration must have one authoritative owner and execution path for each converted operation. Resolve implementation choices within these contracts; a material gameplay change requires a separate design decision.

## Completion checklist

- [ ] The resource prerequisite is delivered and its actual interfaces are used.
- [ ] Weapons and focused scanners share directed operation, targeting/mode and effect-execution contracts.
- [ ] Mode switching preserves commitments, readiness and compatible acquisition progress.
- [ ] Native hits drive explicit shield/compartment/System payload bindings without hidden-target shortcuts.
- [ ] Beams, impact blasts and destruction explosions retain physical behaviour and distinct payloads.
- [ ] Periodic Burning proves independent lifetime, source-aware refresh, removal and exact continuation.
- [ ] Human/AI interfaces, Workshop, content and saves are migrated; obsolete paths are removed.
- [ ] Review findings are resolved and automated/manual evidence is reported accurately under the [testing policy](testing.md).
