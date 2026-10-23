# Shared gameplay engine — migration notes

Delivery direction · 23 October 2026

The [system design](gameplay-engine-design.md) describes the finished architecture. This document records how to reach it and the implementation-level contracts still to specify. Neither document establishes completed implementation or acceptance.

The [shield migration review](gameplay-engine-shield-review.md) compares that design with the current code and separates first-slice decisions from required integration work. Its recommendations remain proposals until resolved.

The [shield implementation plan](shield-migration-plan.md) records the agreed first-slice contracts, staged changes and acceptance gates. The [implementation and verification record](shield-migration-evidence.md) tracks actual delivery and remaining evidence.

## Scope and replacement strategy

Migrate existing gameplay in verified stages. Planned GDD systems inform extensibility but do not become additional implementation commitments in this overhaul. Remove each superseded runtime path when its replacement is usable; the final architecture has one gameplay model.

Migrate maintained content to the new catalogue and definitions. Incompatible old saves require a new game. Do not retain a permanent compatibility runtime. Existing numeric behaviour is preserved except where the agreed design explicitly standardises it, including additive percentage modifiers and the stated shield contribution rules.

Keep native domain behaviour as the starting point. This work changes composition, ownership and common effect execution; it is not an invitation to redesign every damage, flight or sensing algorithm.

## First slice: shields

Start with the smallest practical existing system. Shields are the selected initial candidate, rather than an end-to-end weapon, ammunition, officer and resource overhaul.

The first slice establishes:

1. A reusable shield-effect definition in the shared catalogue, with mod override and reference resolution.
2. A ship-owned shield component receiving contributions from a fitted shield System through that effect.
3. Contribution identity, properties, lifetime and removal without disturbing unrelated contributions.
4. Existing energy, recharge/discharge, fault and damage integration through the new ownership model.
5. Standard shield observations and presentation, with save/restore of actual contribution state.
6. A synthetic second source demonstrating reuse without requiring officer or weapon migration first.

Existing shield dependencies include energy funding, warmup, recharge/discharge, faults and damage absorption. Keep those integration paths usable while migrating shield ownership and effects. Replacing the entire resource network or weapon system is not a prerequisite. Confirm that this boundary stays small during implementation; shields are a candidate based on scope, not a measured claim that they require the fewest lines of code.

Retain the equipment power adapter for the first slice and use an explicitly cost-free temporary grant as the second source. This exercises source-independent shield contributions without requiring general dynamic resource consumers first. The target model permits other grants to declare their own costs and dependencies.

The first slice supports live modifiers for recharge and off-state recharge rates, sustain energy cost on the granting process, discharge rate and available protection. Existing officer/fault facts feed these named properties through the new additive-percentage rule once; structural disablement remains an operating condition. Capacity, initial strength, arc geometry, layer, warmup duration and energy-conversion values remain fixed for each grant in this slice. Live changes to those values require later accounting contracts; this is a delivery limit, not a permanent restriction on the target architecture.

The native absorption implementation currently orders contributions by layer and identity. The agreed design's equal-layer sharing is an explicit change to implement and verify, not behaviour already established by that code. Further shield-specific resolution follows the domain implementation unless a separate design decision changes it.

## Subsequent slices

The second slice, [shared property effects and Duty Officer buffs](property-effects-migration-plan.md), is implemented, including existing fault and boost/impulse adapters on migrated properties. It replaces ordinary officer bonus paths and supports timed scenario and shield-reaction modifiers through shared preparation. See the [verification record](property-effects-evidence.md) for evidence and acceptance limits.

The selected third slice is [resource, store and recipe unification](resource-store-migration-plan.md). It covers all existing material, magazine, energy and coolant stocks, single-type magazine restrictions, shared recipes/processors and existing supply/payment consumers. Implementation is pending.

The fourth slice is [directed systems and payload effects](directed-systems-migration-plan.md), planned after the resource prerequisite. It covers weapons and focused scanners, separate targeting/operating modes, native hit bindings, impact/destruction payloads and periodic conditions. Psychic powers remain a later ability migration. Implementation and acceptance are pending.

Order later slices by the dependencies exposed by the shield implementation and the remaining existing systems. Each slice migrates its maintained definitions, production execution, observations, presentation and continuation together. A broad combat migration is not the prerequisite for proving the framework.

The coverage table in the system design is the inventory to reconcile against delivery. Track each migrated area and its evidence in implementation records; do not infer completion from the existence of catalogue types or a successful build.

## Implementation contracts to specify

The shield interview has settled lifetime versus operation, named independent contributions, funding/dependencies, control/visibility, the first live-modifier set and damage hooks. Hooks run only when damage reaches their layer. First-slice pre-hooks change or negate assigned damage; secondary effects run as independently committed reactions and secondary damage uses a scheduled phase. The shield implementation plan covers these contracts. The remaining items below concern implementation specifications and later framework slices; they do not require reopening settled high-level choices or completing the entire framework before shields.

The high-level design is settled sufficiently to describe the target system. These details belong in implementation specifications before the affected slice is built:

- Concrete definition schemas, parameter bindings and compiled interfaces for rules, effects, contributions, processes, abilities and modes.
- ECS phase placement, event retention across phases/ticks and stable ordering within a phase. Define nested damage-hook execution and transaction boundaries consistently with independent reaction failure.
- Component creation/availability, contribution identities and ownership of composite active effects, including disappearance of a target during staged work.
- Property type/unit representation, valid bounds, expression dependencies and when maintained contributions refresh. Preserve the design's distinction between percentage modifiers and quantity operations.
- Exact store/filter validation, resource amount representation, recipe commitments and processor output handling, without adding tag-based ingredient substitution or extra inventory dimensions.
- Component projection and GUI bindings, including equipment-local detection and permitted publication.
- Schema/save boundary, deterministic continuation encoding and the dependency order of later migration slices.

These details must implement the agreed semantics. A material change to those semantics is a design decision, not an incidental coding choice.

## Evidence

Follow the [testing policy](testing.md). Verification loads resolved production content without fixing shipped tuning values. System integration uses synthetic fixtures through the real loader and runtime. Smoke journeys remain manually triggered evidence.

The first slice should demonstrate a shared template override affecting its consumers, independent contributions from two sources, removal of one contribution, appropriate shield damage behaviour, funding/lifecycle integration and native/WASM continuation. Later slices add meaningful checks for the contracts they introduce. Avoid tests that merely mirror each implementation function or freeze a shipped ship's performance.
