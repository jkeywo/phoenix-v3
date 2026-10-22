# Shared gameplay engine — design draft

Design interview record · 22 October 2026

This document records the agreed direction for the gameplay-engine overhaul. It is a design draft, not an implementation or acceptance record. The [GDD](GDD.md) describes the existing design baseline; this document records proposed replacements separately. The remaining questions at the end must be resolved before treating this as an implementation-ready specification.

## Purpose and delivery boundary

Build a general gameplay engine on the existing entity, equipment, Station, Console, Controller and blackboard foundation. Unify gameplay composition across equipment, Duty Officers, active effects, regions and physical/scenario state trees. Authors compose supported operations; genuinely new physical mechanisms still require C++ implementation.

Migrate existing gameplay rather than implementing the entire outstanding GDD. Planned systems inform the design and its extensibility. Replace existing paths in verified stages, removing each superseded path as its replacement becomes usable. The completed overhaul has one gameplay model, not a permanent legacy runtime.

Maintained content is migrated to the new definitions. Incompatible old saves require a new game. Existing gameplay rules may change where explicitly agreed standardisation replaces them; preserving every old numerical interaction is not the goal.

Specialist Consoles remain the player interface. Built-in components provide dependable state and controls for those interfaces, with reusable presentation for modes, abilities, costs and effects. Accessibility, localisation, permitted observations and human/AI command parity retain their existing contracts.

## Sources, effects and built-in components

The central relationship is:

```text
authored source -> effect -> built-in component
```

The component owns the relevant simulation state and exposes a stable interface to presentation. A fitted System supplies authored behaviour and effect contributions. Other sources can contribute through the same interface.

For example, a ship has a shield component. A fitted shield System applies an effect that contributes shield arcs to it. An officer, scenario or another effect can contribute arcs too. The GUI reads the ship's shield component rather than requiring a custom implementation for each source.

Effects can grant capabilities during play, including capabilities not supplied by the original fit. Removing a temporary contribution removes that contribution, not unrelated contributions or completed consequences. The exact component creation/availability contract remains to be specified.

Keep the existing assembly and fitted-equipment structure. Weapon, scanner, engine and similar equipment become composable templates using supported mechanisms rather than an expanding set of exclusive equipment categories.

| Built-in mechanism | Responsibility and placement                                                                                                                                                           |
| ------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Shield             | Ship-owned shield state, arcs, layers and native protection behaviour; Systems and other sources contribute effects.                                                                   |
| Radar/detection    | Broad detection, acquisition and findings belong to the individual sensing equipment. Information remains contained until published through the existing observation/feed paths.       |
| Turret             | Physical aiming, traversal and mount state belong to the particular turret.                                                                                                            |
| Directed system    | A shared core for weapons, directed scanners and other directed operations, including targeting and firing/operating modes. What happens when it operates is supplied through effects. |
| Store              | Resource contents, capacity and supply on the appropriate storage instance. Magazines, batteries, coolant stores and cargo holds use this mechanism.                                   |
| Engine             | Continuous interaction with movement physics, with exposed properties and effect extensions.                                                                                           |
| Processor          | Continuous or batch execution of authored recipes, including inputs, work, outputs and output-capacity handling.                                                                       |

Directed scanners share operating machinery with other directed systems without pooling their evidence. Their acquisition and findings remain equipment-local.

Built-in mechanisms include common behaviour and UI contracts; authors do not rebuild shields, aiming or physics from low-level rule fragments. They remain extensible with effects. Existing domain implementations supply detailed physical rules unless this interview explicitly changes them.

Shield contributions are independent and have a layer value determining hit order. Equal-layer contributions share damage evenly. Further shield-specific damage-resolution details follow the existing implementation; this framework design does not introduce a new redistribution algorithm.

Heat remains an equipment property, with supported heating and cooling operations, thermal limits and cooling behaviour. It is not an inventory resource. Cooling may consume coolant from ordinary stores.

## Authored rules and authority

Trusted server-authored rules can perform any supported operation on any target and can read authoritative world state, including facts no Console has observed. There is no effect-source authentication or ownership permission tier separating an officer, equipment, a region or a scenario tree.

An objective may fill a ship's battery directly. An officer's authored ability may delete the sun. Costs, range and qualifications apply where the author specifies them or where they are intrinsic to the chosen operation. A source category does not impose additional restrictions.

Operations still have meaningful contracts. Transferring a resource conserves it; setting or creating a quantity explicitly changes it. Ordinary movement and explicit relocation are different operations. Authors can choose the appropriate operation without disguising an exception as a physical transfer.

Source identity is bookkeeping for stacking, lifetime, continuation and diagnostics, not permission. It is not necessary to authenticate an officer or objective as though it were a client.

Controllers retain their role in operating intent. Incoming player commands still use the existing admission boundary. Controller AI trees read permitted observations and submit ordinary commands; they do not gain direct effect application or private world-state access. Physical, team, away and scenario trees can apply effects. Authored simulation access to truth does not automatically publish that truth to players or Controller AI.

Existing command ownership and explicit capture behaviour remain the control boundary. The unrestricted authored operation vocabulary is not a client-facing arbitrary mutation interface.

## Behaviour and effect lifecycle

Use compact trigger/requirement/target/cost/effect rules for ordinary behaviour. Use existing state-tree concepts when work requires sequencing. Do not require a state-tree instance for every passive modifier or continuous process.

Continuous processes coexist with activated abilities. Generation, ordinary thrust and passive cooling can be processes; firing and jumping can be initiated work. Both use shared requirements, costs, properties and effects.

Optional automatic abilities belong to explicit authored operating modes selected or enabled through normal operating intent. Inherent reactions, such as a damage-caused fault, do not require a player to enable them first.

Officer abilities declare their requirements individually. Some need a choir, team or other apparatus; others can originate from the officer alone. There is no universal apparatus requirement.

Requirements, costs and effect values support bounded typed expressions over available source, target and event properties. This permits relationships such as damage depending on source heat without requiring arbitrary scripting or author-defined physical primitives.

An active effect may contain its own triggers and reactions. A reusable Burning effect can define periodic damage, spreading and extinguishing behaviour. Applying Burning installs that behaviour; its removal stops future behaviour without undoing damage already inflicted. A simple modifier need not contain triggers.

### Properties and combination

Effects modify explicit properties such as scan range, generation rate or energy cost. There is no universal component effectiveness value that silently scales all capabilities. Reusable effect bundles can change several named properties together.

Content and mods can declare new typed gameplay properties without adding C++. Conditions, costs and effects can read and change these properties. A custom property such as psychic strain gains its behaviour through authored rules that use it. Built-in properties such as engine thrust retain their native meaning; declaring a custom property does not introduce a new physical mechanism.

Comparable numeric modifiers use one additive-percentage rule: two +25% contributions produce +50%; +50% and -50% cancel. This replaces the special officer positive/negative formula and strongest-only treatment for comparable modifiers. Actual resource quantities, damage and transfers are operations rather than percentage contributions. Attribute bounds and treatment of zero-valued bases still need an explicit contract.

Reapplying the same timed modifier from the same original source to the same target refreshes one contribution rather than adding another. Refresh must not shorten its remaining duration. Different sources' contributions add.

Projectiles and chains of effects retain their original source for this purpose. Two missiles from one gun do not become independent modifier sources merely because they are separate projectile entities. Sources include originating equipment, officers, regions and tree instances. Exact persistent source-key encoding remains an implementation decision to document.

### Applied and maintained contributions

Applied effects capture the values supplied at application. Their values remain until refresh or removal. Maintained effects continue evaluating their contribution, for example as funded output changes.

Ongoing dependencies are explicit and separate from value evaluation. Losing a granting officer or component stops or pauses work that still requires it. Independent results, including launched projectiles and already inflicted conditions, continue unless their own authored rules say otherwise.

Ongoing resource shortages use a small shared set of supported responses: proportional output, pause or stop. Indivisible operations require complete payment. Cancellation or shortage does not automatically refund completed work.

### Immediate execution and damage hooks

A single immediate execution step commits its required effects and costs together. Validate the required targets and operations, secure the costs, and apply the group. Failure leaves no partially paid or partially applied required group. A later state-tree stage or projectile impact is a separate execution and cannot undo earlier completed work.

A triggered reaction is a separate execution step with its own costs and failure outcome. Its failure does not undo the event that triggered it. For example, damage remains inflicted when a defensive discharge cannot pay its energy cost. Pre-damage hooks can still explicitly modify or cancel the pending hit.

Effects can run before or after damage to a shield, compartment or System. Pre-damage effects can change or cancel the pending hit and perform supported operations. The built-in damage mechanism processes the resulting hit. Post-damage effects receive the actual outcome, such as absorption, inflicted damage or resulting faults.

This supplies extension points around native damage processing. It does not replace every damage mechanism with a generic authored simulation. The precise interaction between nested hooks, atomic steps and failure remains an open framework question.

### Trigger processing order

Order the ECS systems and phases so triggers are processed at the appropriate point in the simulation. Trigger production, reaction processing and built-in operations participate in that schedule, including the stages before and after damage. A separate recursive reaction scheduler is not required by this design.

The schedule must preserve stable processing order and the existing Controller observation boundary described in [simulation](simulation.md) and [Controller architecture](controller-architecture.md). Whether a generated event is consumed later in the current tick or in a subsequent tick follows its producer and consumer phases; there is no blanket same-tick or next-tick rule for all effects. Exact phase placement and its interaction with atomic execution remain to be specified.

## Shared gameplay catalogue

The selected game packs provide a static, mod-extensible catalogue of resources, recipes, effects and other reusable gameplay templates. This includes reusable definitions for abilities, processes, operating modes and equipment compositions where appropriate. Runtime quantities, active effects and work progress remain separate from those definitions. Use the existing dependency ordering, stable definition IDs and explicit override model described in [content format](content-format.md); adding a resource does not require another C++ enum value.

Content references shared definitions rather than copying their contents into every consumer. Mods can override a shared effect or template to make sweeping changes across its users. Resolve references and inheritance against the final selected catalogue after explicit pack overrides, retaining explicitly authored specialisations under the existing inheritance rules. For example, changing a shared shield contribution template changes the equipment and other sources that use it, subject to their explicit specialisations. This is catalogue composition at load time; runtime instances still own their individual state.

### Resources and recipes

Resources have tags such as ammo, energy and cargo. Store acceptance filters combine tag matches and exact resource IDs using AND/OR. Acceptance and capacity are separate concerns.

A resource defines its size under each relevant tag. For example:

| Resource    | Ammo size | Cargo size |
| ----------- | --------: | ---------: |
| Small shell |         1 |          2 |
| Heavy shell |         1 |          8 |

A store selects the tag used to measure capacity. A magazine measuring ammo can count rounds, while a hold measuring cargo accounts for bulk. Mixed resources consume that shared capacity using their respective sizes. This is not one global size for every resource sharing a tag, nor an independent capacity bin for every resource ID.

Resources can reference attached effects. The using System decides whether, when and how to use them. Burning ammunition may supply a Burning effect, while the gun defines range, targeting and firing behaviour and may pass that effect into a projectile for application at the appropriate time. Storage, transfer or consumption alone does not automatically execute the attached effect.

Recipes are also named, tagged catalogue definitions. Processors can identify supported recipes through tags and/or exact IDs. Recipe inputs and outputs name specific resources; there is no tag-based ingredient substitution. Recipes define inputs, outputs and required work, and the processor supports continuous or batch execution with effect extension points.

Detailed capacity validation, resource units and processor execution interfaces remain to be specified. Existing accounting and work contracts are the starting point rather than opportunities to invent extra inventory mechanics.

## Migration coverage and evidence

The inventory includes current and planned mechanics. Existing implementations to migrate include officer expertise/qualifications, psychic powers, officer/team conditions, weapons and ordnance, turrets, magazines, shields, propulsion, sensors/findings, communications, power/cooling, faults, repair/Medical, existing supply paths, craft/transport, FTL departure, away/boarding operations, regions and scenario consequences.

Planned hazards/containment, general logistics/conversion, tractors, additional signal/psychic operations, full passage, facilities and campaign consequences inform extensibility but are not additional delivery commitments in this overhaul.

Start with the smallest practical existing system rather than a broad combat slice. Shields are the selected initial candidate. The first slice should establish a reusable shield-effect template in the catalogue, a fitted System contributing through that effect to the ship's shield component, its lifecycle and properties, and integration with existing damage processing and presentation. A synthetic second source can demonstrate that the contribution is reusable without first migrating officers or weapons.

Existing shields depend on energy funding, recharge/discharge, faults and damage absorption. Preserve those integration paths while migrating shield ownership and contributions; the first slice does not require replacing the entire resource network or weapon system. Detailed shield behaviour follows the earlier decisions in this document. Validate the dependency boundary before treating shields as the smallest implementation slice.

Verification follows [testing policy](testing.md). Use synthetic production-loader scenarios to check cross-source composition, modifier refresh and source lineage, applied versus maintained values, atomic payment/application, interruptions, pre/post hooks, resource filters and capacity, processor accounting, equipment-local knowledge, and native/WASM save continuation. Production content verification must permit balance changes. GUI, accessibility, physical-device and performance journeys remain separate manually triggered evidence.

No implementation, tests or gameplay acceptance are claimed by this draft.

## Remaining design branches

- Exact reusable definition boundaries and interfaces for rules, effects, contributions, processes, abilities and operating modes.
- Placement of trigger producers and consumers in the ECS schedule, and the relationship between pre/post hooks, generated events and atomic execution steps.
- Removal, replacement, interruption and failure behaviour for composite effects, including targets disappearing during execution.
- Property units/bounds, expression dependencies and the precise update/refresh rules for maintained values.
- Catalogue/store validation and processor bindings sufficient to migrate existing resource paths without extra inventory features.
- Component and GUI integration contracts, including contribution identity, standard observations and equipment-local detection.
- Dependency-ordered migration slices, content changes, schema boundary and acceptance cases for each slice.

Resolve these as framework questions. Keep detailed shield, flight, sensing and other domain algorithms with their built-in implementations unless an explicit design decision changes them.
