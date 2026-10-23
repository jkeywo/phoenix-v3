# Shared gameplay engine

Target design · 23 October 2026

This document describes the gameplay system after the overhaul is complete. It is a design contract, not a claim of implementation or acceptance. It refines the [GDD](GDD.md) for effects, abilities, properties, resources and reusable gameplay composition. The [migration notes](gameplay-engine-migration.md) describe delivery separately.

## Purpose

Phoenix uses one shared gameplay framework for equipment behaviour, officer buffs and abilities, conditions, regions and authored world consequences. Sources define when something happens, under what conditions, to which targets and at what cost. Effects perform supported operations or establish ongoing behaviour on those targets.

The framework sits on the existing entity, fitted-equipment, Station, Console, Controller and blackboard architecture. Native components retain physical simulation and dependable presentation interfaces. Authors assemble those mechanisms into reusable definitions rather than reconstructing physics or GUI contracts for every ability.

The finished system has one model for migrated gameplay. Officer bonuses, psychic powers and scenario consequences use the same effect machinery as equipment. Specialist Consoles remain the player interface.

## Core concepts

| Concept              | Responsibility                                                                                                          | Example                                                                        |
| -------------------- | ----------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| Catalogue definition | Shared, immutable authored content selected from game packs and mods.                                                   | A shield template or Burning effect definition.                                |
| Source               | The origin of an application or maintained contribution. Its identity supports lifetime, reapplication and diagnostics. | A fitted generator, officer, region or scenario-tree instance.                 |
| Rule                 | A trigger together with conditions, target selection, costs and effects.                                                | On a successful hit, apply the ammunition's payload to the struck compartment. |
| Ability              | Work that can be activated, subject to its authored requirements and costs.                                             | A psychic attack or deliberate shield discharge.                               |
| Process              | Behaviour that runs while its operating conditions hold, with ongoing costs where declared.                             | Generation, thrust or cooling.                                                 |
| Operating mode       | An authored selection of operating behaviour and enabled optional automation.                                           | A gun's firing mode or a directed scanner's scan mode.                         |
| Effect               | A reusable description of immediate operations, persistent contributions or reactive behaviour.                         | Fill a battery, grant a shield arc, modify scan range or apply Burning.        |
| Active effect        | A runtime instance of a lasting effect, with its own target, source, values, lifetime and relevant reaction state.      | Burning on a particular compartment.                                           |
| Contribution         | An identifiable part of a component's capability or effective properties supplied by an active effect.                  | One shield layer or one officer's range bonus.                                 |
| Built-in component   | The owner of native simulation state and the interface consumed by ordinary presentation.                               | Ship shields, a turret mount or an equipment-local detector.                   |

These are conceptual boundaries, not a requirement for a separate ECS entity or C++ class for every row. A simple modifier does not need its own state tree.

The central relationship is:

```text
catalogue definition
        |
        v
source -> rule / ability / process -> effect -> target component
                                              |
                                              v
                                  permitted observations -> Console
```

A source can apply several effects, and a component can receive contributions from several sources. A shared definition never implies shared mutable runtime state.

## Shared catalogue and templates

The selected packs supply a static, mod-extensible database of resources, recipes, effects, property declarations and reusable gameplay templates. Templates cover abilities, processes, operating modes and equipment compositions, alongside the existing entity, compartment and assembly definitions.

Definitions have stable IDs and reference other definitions. A weapon references an effect instead of carrying a private copy of its implementation. Equipment can inherit a common template and specialise the parameters or structure that differ.

Catalogue composition follows the existing [content rules](content-format.md):

- Packs declare dependencies and explicitly name the definitions they override.
- An override replaces a definition of the same kind.
- References and inheritance resolve against the final selected catalogue.
- Inheritance uses the existing merge rules: objects merge, arrays replace and null removes a field.
- Missing references, incompatible kinds and inheritance cycles are validation errors.

A mod can therefore change one shared shield template or payload effect and affect all definitions that use it. Explicit specialisations retain their declared differences. Sweeping changes depend on shared references and inheritance, rather than finding and rewriting copied content.

The catalogue is fixed for a running game. Stores, active effects, ability execution and processor work hold mutable instance state separately. Compilation resolves authored names and expressions into typed runtime data; gameplay does not interpret JSON every tick.

Adding content using supported mechanisms requires no C++ enum entry per resource, effect or custom property. A genuinely new physical primitive still requires implementation.

## Built-in mechanisms

Built-in mechanisms integrate common behaviour with stable state and GUI interfaces. Effects configure, contribute to and extend those mechanisms.

| Mechanism                | State owner and native responsibility                                                                                         | Effect integration                                                                                       |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| Shields                  | Ship component holding independent shield contributions, arcs, layers and protection state.                                   | Equipment or other sources grant contributions and modify their properties; damage has pre/post hooks.   |
| Radar and detection      | Individual sensing equipment holding acquisition state and findings.                                                          | Effects modify sensing properties or perform supported sensing operations. Publication remains explicit. |
| Directed systems         | Equipment implementing shared targeting and firing/operating modes for guns, directed scanners and other directed operations. | The operation's outcome is an effect or effect group.                                                    |
| Turrets                  | Individual mount holding physical aim, traversal and tracking state.                                                          | Effects alter supported properties or add supported behaviour around operation.                          |
| Stores                   | Individual storage instance holding resource quantities, acceptance rules and capacity.                                       | Effects consume, transfer, add or set resources through defined operations.                              |
| Engines                  | Fitted propulsion mechanisms interacting continuously with ship physics.                                                      | Processes provide funded operation; effects alter explicit properties or invoke supported operations.    |
| Processors               | Equipment holding recipe selection and batch or continuous work progress.                                                     | Recipes define inputs, outputs and work; effects extend supported execution points.                      |
| Damage and thermal state | Relevant compartments and equipment holding structural condition, faults and heat.                                            | Effects apply damage, conditions, repairs, heating or cooling through native operations.                 |

Equipment is composed from these capabilities within the existing assembly and fitted-System structure. A directed weapon may combine a turret binding, stores, an operating mode, resource costs and an effect payload. A scanner uses the same directed operating machinery while retaining its own sensing state.

Each directed capability has one active operating mode and a separately selected targeting mode, with supported combinations declared by the equipment. Target and magazine selections remain separate. An assembly can contain multiple directed capabilities for simultaneous operations. Changing operating mode stops the old sustained operation immediately at the normal input boundary, while preserving cooldowns, loading commitments and compatible scan progress. Launched projectiles and independent inflicted effects continue.

Payloads bind explicitly to native shield-hit and compartment/System-hit outcomes. Native resolution determines which layers and locations are reached; a selected target does not substitute for the actual hit. Impact and destruction payloads use shared effect execution while retaining separate definitions and physical delivery rules. The [directed-system migration plan](directed-systems-migration-plan.md) defines this slice, including periodic effects proved by burning ammunition.

Effects can grant temporary capabilities that were absent from the original fit. The native component owns those capabilities' actual state. Removing a contribution removes that source's grant without deleting unrelated contributions or reversing completed consequences.

### Shields

A ship owns its shield component. A fitted shield System applies an effect that contributes shield arcs and associated state. An officer, region or scenario can contribute through the same mechanism.

Contributions are independent. Their layer values determine hit order; equal-layer contributions share damage evenly. Further shield-specific resolution follows the native shield design. The generic framework does not prescribe a replacement shield algorithm.

Each named shield contribution owns one arc, an independent reserve and its own warmup state. A multi-arc effect contains several named contributions. Contribution identity is the active-effect instance plus a stable local name, such as fore or aft. Updating an existing contribution preserves its identity and accumulated state; it does not duplicate the grant or reset its strength. Different effect instances using the same template have independent contributions. Original source identity remains separate for modifier stacking and diagnostics; the timed-modifier refresh key does not merge distinct named shield contributions.

Shield GUI reads the ship component's standard projection. It does not need separate implementations for equipment-generated shields and officer-generated shields.

All shield contributions use a common observation interface through the permitted shield feed, including their arcs, strength, warmup and operating state. Human operators and Controller AI receive the same permitted information. Visibility does not grant control: equipment retains its command bindings, and other grants expose controls only when authored. A scenario shield can be visible without offering lower or discharge commands; an officer's shield can expose dismissal through its ability. Other ships discover contributions through normal sensing, which need not reveal the granting source's identity.

The granting ability or process declares costs and operating dependencies. Equipment-backed shields use their equipment's existing power allocation, enabled state and faults; sustain receives priority within the funded grant and recharge uses the remainder. Officer grants use their authored costs and dependencies, while a scenario grant can have neither costs nor an ongoing source dependency. There is no automatic generator or battery requirement on every shield contribution. Failed funding or dependencies change operation according to the authored rules while preserving the contribution under its lifetime contract. Discharge requires an explicit destination store and conversion rule.

### Detection

Detection belongs to equipment. A directed scanner sharing targeting machinery with a gun does not move its findings into a universal ship-wide detection component.

Acquisition, retained findings and permitted publication keep their existing distinctions. Sharing an effect definition or mounting two capabilities together does not merge their knowledge. Ordinary feeds and reports deliver the information that other Consoles are permitted to receive.

### Heat

Heat is an equipment property, not a stored resource. Native thermal behaviour exposes supported heating, cooling and limit interactions. Cooling can consume coolant from a store. A resource quantity and a thermal property have different meanings even when the same process changes both.

## Rules, abilities and processes

A rule expresses five questions:

1. **Trigger:** what occurrence or operating condition causes evaluation?
2. **Conditions:** what must be true for this execution?
3. **Targets:** which entities, compartments, equipment or other supported objects are affected?
4. **Costs:** what must be paid immediately or while the work continues?
5. **Effects:** which operations or ongoing contributions are applied?

The source, selected targets and triggering event provide typed context. Bounded expressions can use their properties to calculate conditions, costs and effect values. An effect can, for example, derive damage from source heat. Expressions compose supported behaviour; they are not an unrestricted scripting language.

Compact rules handle ordinary passives and reactions. State trees handle sequencing, waiting, branching and multi-stage work. Physical, team, away and scenario trees can apply effects directly. They reuse the same execution and cost contracts as other sources.

Continuous processes and activated abilities share conditions, targets, costs, properties and effects. Generation and ordinary thrust do not need to be disguised as repeated button presses. Finite jobs retain their readiness, progress, interruption and outcome semantics.

Operating modes expose supported choices such as firing pattern, scan mode or optional automatic ability use. Inherent reactions, such as damage producing a fault, do not require a player to enable them.

An officer ability declares its own requirements. It may require a choir, team or apparatus, or operate from the officer alone. No universal apparatus requirement applies.

## Authority and knowledge

Trusted server-authored content can perform any supported operation on any target. Its conditions and selectors can read authoritative world state, including unobserved facts. There is no permission hierarchy between effects originating from equipment, officers, regions or objectives.

An objective can fill a battery directly. An officer ability can delete the sun. Costs, range and qualifications apply when authored or intrinsic to the selected operation; source identity does not impose additional permissions.

Operations retain explicit meanings. A transfer conserves the transferred resource. A fill, set or create operation deliberately changes a quantity. Movement and relocation are distinct operations. Authors select the operation that expresses their intended consequence.

Source identity supports stacking, lifetime, continuation and diagnostics. It is not authentication, and direct authored consequences do not require a player or officer identity to justify them.

The [Controller boundary](controller-architecture.md) remains intact. Incoming player commands use normal admission. Controller AI reads permitted observations and submits ordinary operating commands; it does not directly apply effects or inspect hidden world state. Trusted physical/scenario execution and Controller decision-making remain different contexts, even when both use state trees.

An effect's access to world truth does not publish that truth. Telemetry, sensing, reports and feeds still determine what humans and Controller AI know. Capture and command ownership retain their existing contracts.

## Properties and modifiers

Effects address explicit named properties: scan range, energy cost, recharge rate, thrust or another supported value. There is no universal effectiveness property that silently scales every capability of a component. A reusable effect can modify several named properties as a bundle.

Content and mods can declare typed custom properties, such as psychic strain. Conditions, costs and effects can read and change them. Their gameplay meaning comes from the rules that use them. Built-in properties retain their native meaning; declaring a new property does not create new physics.

Property interfaces declare their types and applicable units and bounds. Expressions and operations must be compatible with that interface. Native quantities retain their mechanism's meaning, including distinctions between a capacity, a current amount and an operating rate.

Comparable percentage modifiers combine additively against the unmodified value:

```text
effective value = base value × (1 + sum of percentage contributions)
```

Two +25% contributions give +50%; +50% and -50% cancel. Property-specific valid bounds still apply. A percentage modifier does not create a nonzero quantity from a zero base.

Resource transfers, inflicted damage and direct changes to counters are quantity operations, not percentage modifiers. This shared combination rule replaces the previous special officer positive/negative calculation and strongest-only treatment for comparable modifiers.

## Effect lifecycle

An immediate effect performs its operation and completes. A lasting effect creates runtime state for its contributions, duration, dependencies and any reactions. The definition is reusable; each application has its own relevant execution state.

### Applied and maintained values

Applied effects capture supplied values when applied. Those values remain until refresh or removal. Maintained contributions reevaluate their values as the relevant operating inputs change, such as funded output.

Value evaluation and lifetime dependency are separate. A launched projectile or inflicted condition can continue independently of its source. A maintained grant can depend on the source remaining available, enabled or funded.

Loss of an ongoing dependency invokes the authored pause or stop behaviour. Completed damage, transfers and other independent consequences are not undone.

Equipment-granted shield contributions persist while the equipment is installed. Installation creates the contribution and its initial reserve once. Enablement, funding and damage affect operation rather than removing and recreating the contribution. Lowering a shield or losing power removes coverage while preserving stored strength and fractional accounting; existing off-state recharge and discharge rules still apply. Raising it resumes the same contribution through normal warmup. Refreshing or updating its effect does not refill it.

Removing the equipment removes its shield contribution and any remaining strength without an automatic refund. Temporary shields follow their authored duration and dependencies: expiry removes the contribution, while switching protection off need not expire it. Explicit fill operations can change the reserve when intended.

### Reapplication and source identity

Reapplying the same timed modifier from the same original source to the same target refreshes that contribution rather than accumulating another copy. Refresh never shortens its remaining duration. Contributions from different sources add.

Projectiles and effect chains retain their original source for this purpose. Two missiles from one gun do not acquire independent modifier sources merely because they are different projectile entities.

A contribution's identity also permits removing the right grant. Removing one officer's bonus leaves another officer's contribution intact. The source can remain identifiable for bookkeeping after its physical object has ceased to exist; that does not keep the object operational.

### Reactive effects

An active effect can own triggers. Burning can contain periodic damage, spreading conditions and extinguishing behaviour. The gun applies Burning; it does not have to reproduce Burning's internal rules.

Removal stops that instance's future reactions and maintained contributions. It does not reverse damage already caused. Consequences that are independent applications follow their own lifetime rules.

### Costs and interruption

Immediate costs belong to the execution step they fund. Ongoing costs belong to the continuing process or ability. Supported shortage responses are proportional output, pause and stop. An indivisible operation requires complete payment.

Cancellation stops the relevant future work. It does not automatically refund completed work, recover fired ammunition or reverse a committed effect. Later stages validate their own requirements, including whether their targets still exist.

## Execution and trigger order

Flecs systems and phases determine when triggers are produced and consumed. The schedule places reactions at the appropriate points around native operations. Definitions compose supported triggers and mechanisms; they do not program the global ECS processor order.

A generated event can be consumed in a later phase of the same tick or a subsequent tick, according to its producer and consumer. There is no universal same-tick or next-tick rule for every effect, and no requirement for an independent recursive reaction scheduler.

Processing preserves deterministic ordering independently of ECS storage order. The existing fixed-tick simulation and frozen Controller observation boundary remain: physical results do not become visible early to one Controller because its table happened to be visited later.

### Atomic execution steps

A single immediate execution step commits its required effects and costs together. It validates required targets and operations, secures costs and commits the group. Failure leaves no partially paid or partially applied required group.

A later tree stage, projectile impact or triggered reaction is a separate execution step. Its failure cannot undo earlier completed work. Damage remains inflicted if a resulting defensive discharge cannot pay its energy cost.

Discrete requests retain execution identity and receipts so retries, subscriptions and restore cannot repeat a committed operation.

### Damage hooks

Shield, compartment and System damage expose pre-damage and post-damage stages:

```text
pending hit -> pre-damage effects -> native damage resolution -> post-damage effects
```

Pre-damage effects can modify or explicitly cancel the pending hit and perform supported operations. Native resolution applies the resulting damage. Post-damage effects receive the actual result, such as absorbed energy, inflicted damage and resulting faults.

Shield hooks run per affected contribution, and only when damage reaches its shield layer. Contributions in later layers receive no hooks when earlier protection has already stopped the damage. Native arc, layer and sharing rules determine the damage presented to each contribution; its pre-hooks run before absorption and its post-hooks receive the actual absorption, remaining damage and resulting shield state. Hook context identifies both the original hit and the affected contribution.

The first shield migration restricts pre-hooks to changing or negating the damage assigned to that contribution. Negated damage is removed; it does not bypass the shield or continue to later layers. Secondary effects execute as separate reactions after the native damage operation. Secondary damage enters a scheduled reaction phase rather than recursively resolving inside the current hit. Each reaction commits its own costs and required effects; its failure leaves the original damage result intact. This delivery scope is detailed in the [shield implementation plan](shield-migration-plan.md); broader supported pre-hook operations remain part of the target framework.

These are extension points around the physical damage mechanisms. The framework does not require every part of penetration, shield absorption or fault selection to be expressed as generic authored rules.

## Resources, stores and recipes

### Resource definitions

Resources are catalogue entries with stable IDs and tags such as ammo, energy or cargo. A resource can reference an attached effect. The consuming mechanism decides whether, when and how to use that effect.

Burning ammunition supplies a Burning effect. Its gun supplies range, targeting and firing behaviour and can pass the payload to a projectile for application on impact. Storing, transferring or consuming that ammunition does not automatically set anything on fire.

### Acceptance and capacity

Stores share one mechanism across magazines, batteries, coolant tanks, parts stores and cargo holds. A store can accept mixed contents. Filters combine tags and explicit resource IDs using AND/OR; acceptance is separate from capacity.

A store can optionally be restricted to one resource type. Magazines use this restriction: different ammunition choices occupy separate magazines. The allowed type can be a specific resource or chosen from a tag-filtered set. A chosen type remains fixed when the store becomes empty; changing it is deliberate and requires an empty store with no outstanding commitments, including loaded-round returns or incoming deliveries. General stores may retain mixed contents.

A resource declares a size for each relevant tag. A store selects the tag by which it measures capacity.

| Resource    | Size under ammo | Size under cargo |
| ----------- | --------------: | ---------------: |
| Small shell |               1 |                2 |
| Heavy shell |               1 |                8 |

A magazine using the ammo tag counts both shells as one unit. A cargo hold using the cargo tag accounts for their different bulk. Mixed contents share capacity:

```text
used capacity = sum(quantity of each resource × its size under the store's capacity tag)
```

This is a size per resource/tag pair. It is neither a universal size for everything with a tag nor an independent capacity allowance for each resource. An accepted resource must have a usable size for the store's selected capacity measure.

Resource movement and supply continue to use actual connected stores and supported work. Unifying the stored quantities does not create automatic access to every store in the world. Explicit authored fill/set operations remain available for consequences that are intentionally independent of supply.

### Recipes and processors

Recipes are tagged catalogue definitions with specific input resources, specific output resources and required work. A processor selects supported recipes by tag and/or exact ID.

Ingredients name exact resource IDs. There is no tag-based ingredient substitution. Processors handle batch or continuous progress, consumption, output capacity and their supported effect extension points.

Recipe definitions are shared. Each processor keeps its own progress and commitments. Completed consumption or output is actual state, subject to the same execution and interruption contracts as other work.

## Gameplay coverage

The shared framework supplies composition and lifecycle across these areas. Native mechanisms continue to own their specialist simulation and information contracts.

| Gameplay area                                           | Shared representation                                                             | Domain behaviour retained                                             |
| ------------------------------------------------------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| Duty Officers                                           | Requirements, property modifiers, granted abilities and maintained contributions. | Roster, assignment, travel and qualification facts.                   |
| Psychic operations                                      | Abilities, sustained processes, costs, conditions and effects.                    | Required apparatus, supported psychic mechanisms and scoped findings. |
| Weapons and ordnance                                    | Directed modes, firing costs, payload effects and reactive conditions.            | Aiming, projectile flight, guidance, collision and penetration.       |
| Turrets and directed scanners                           | Shared target/operating interfaces and property contributions.                    | Mount motion and equipment-local acquisition/evidence.                |
| Shields                                                 | Effects contributing native arcs/layers and property changes.                     | Shield state, damage resolution and standard presentation.            |
| Magazines, batteries and cargo                          | Resources, filters, capacity, transfers and recipes.                              | Supply connectivity, loading, commitments and delivery.               |
| Generation, propulsion and cooling                      | Continuous processes, funding and named property modifiers.                       | Resource allocation, thermal behaviour and continuous physics.        |
| Faults, repair and Medical                              | Conditions, application/removal rules, costs and work outcomes.                   | Diagnosis, access, teams, treatment and repair progression.           |
| Teams, boarding and away missions                       | Requirements, staged effect execution and reusable consequences.                  | Personnel, transport, encounters and command ownership.               |
| Carried craft and FTL departure                         | Preparation costs, processes, conditions and completion effects.                  | Craft entities, movement, servicing and departure workflows.          |
| Communications and sensing                              | Supported operations, costs, property changes and conditions.                     | Signal delivery, conversations, observation provenance and scope.     |
| Regions and fields                                      | Entry/exit or periodic rules, maintained contributions and hazards.               | Region geometry, membership and native field interactions.            |
| Objectives and scenario trees                           | Direct supported operations and reusable effect references.                       | Scenario progression, outcome evidence and reporting.                 |
| Future facilities, conversion and campaign consequences | The same recipes, processes, properties and effect interfaces.                    | New domain mechanisms when they are actually introduced.              |

Planned systems inform the interfaces. Their inclusion here does not add all remaining GDD features to the overhaul's delivery scope.

## Worked examples

### Two sources contributing shields

A fitted shield System references a shared shield-effect template and supplies its configured arc and operating values. Its maintained effect contributes to the ship's shield component through the normal funding and operating rules. A scenario applies a temporary shield contribution to that same component.

Both contributions participate in native protection and the standard shield display. Expiry of the scenario effect removes its contribution; the fitted System's contribution retains its state. An override of their common template changes the inherited definition values when the game catalogue is loaded, while the two runtime contributions remain independent.

### Burning ammunition

A gun's directed mode selects a target and initiates firing. The firing step secures its required ammunition and energy and launches the projectile. The ammunition definition references Burning; the gun chooses to carry that effect as the projectile's impact payload.

At impact, the native hit path resolves shields and the struck material. The payload applies Burning at the point selected by the weapon's supported impact behaviour. Burning then owns its periodic reactions and extinguishing rules. Later hits from the same original gun refresh the same timed modifier on the same target. Destroying the gun does not automatically extinguish an already inflicted independent condition.

### An objective filling a battery

A physical scenario tree completes an objective and directly applies a fill operation to a selected store. The operation uses the store's native resource and capacity contract. It does not require an officer source, ownership permission or fabricated supply transfer. The resulting quantity is actual state; normal telemetry determines when and where the change becomes visible.

## Presentation and authoring

Specialist Consoles read stable projections from built-in components. Reusable presentation covers operating modes, abilities, costs, active effects and reasons work cannot proceed. A shield display understands shield state regardless of which source granted each contribution.

Player presentation and Controller AI consume the same permitted observations. Neither receives hidden diagnoses or target facts merely because an authored rule used them. Requested settings, funded operation, actual output and known conditions remain distinguishable.

Workshop exposes catalogue references, inherited values, local specialisations and override origins. Authors can inspect which definitions depend on a shared template before making broad changes. Validation reports the affected definition, field or binding and reason.

Runtime inspection relates active effects to their definitions, original sources, targets, contributions and lifecycle state. Cost failures and unmet conditions explain blocked execution without inventing a separate gameplay model.

Player controls retain keyboard, touch, gamepad and screen-reader semantics. Localisation covers authored labels, effect descriptions, reports and tools. Standard component interfaces prevent each new effect from requiring a bespoke Console implementation.

## Persistence and validation

Save/restore preserves active effects, source lineage, durations, captured values, ongoing dependencies, ability/process progress, resource quantities and committed execution identities. Restoring the world does not reapply completed one-shot effects or refill stores by reconstructing definitions.

Compiled lookups and other derived data can be rebuilt against the selected catalogue. Stable logical identities and deterministic continuation apply across native and WASM execution.

Content verification checks the resolved catalogue: references, property/expression compatibility, native interfaces, resource filters and capacity measures, recipe inputs/outputs, physical bindings and presentation requirements. It checks valid authored data without freezing balance values.

Synthetic integration scenarios exercise cross-source contributions, refresh and removal, atomic payment/application, independent reaction failure, damage hooks, equipment-local knowledge, store accounting and save continuation through production paths. Manual smoke evidence covers player journeys, accessibility, devices and performance according to the [testing policy](testing.md).

The [migration notes](gameplay-engine-migration.md) retain delivery choices and implementation-level specification work. They do not change the finished system described here.
