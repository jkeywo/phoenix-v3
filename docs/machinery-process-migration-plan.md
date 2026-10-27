# Shared machinery processes — eighth migration slice

Approved 27 October 2026. Implementation base: `e127b52`, after the custom-property and effect consolidation. This contract describes required implementation; verification is recorded separately.

## Intended result

Migrate generation, propulsion, cooling and existing recipe processors onto shared continuous processes. Include processes composed entirely from effects and temporary process grants. Preserve production consumption, tuning and physical behaviour. FTL charging, repairs, Medical work and craft servicing remain later migrations. Existing boost and impulse behaviour stays within this propulsion migration.

## Process model and interfaces

- Add immutable `process` and `process_grant` templates to the existing catalogue, with named declarations on entities, equipment, officers and compartments. Reuse canonical native-owner addressing and existing effect application identities.
- Support several concurrent processes per owner. Reuse explicit exclusion groups and conflict rejection; additionally prevent two processes from driving the same native mechanism simultaneously.
- Native machinery adapters require compatible fitted equipment. Other owners can host effect-only processes. Officer duty, presence and health requirements are explicitly authored.
- Definitions contain requirements, initial requested state, entry costs, running upkeep, work-proportional costs, lifecycle effects and work effects. Reuse the existing expression compiler, payment machinery and effect transactions.
- Processes start automatically unless initially disabled. Temporary blocking pauses the same run, preserving identity, credits, native progress and requested intent. Resume does not repeat entry costs. Explicit stop, expiry or true termination ends the run; a subsequent start pays entry costs again.
- Refresh preserves execution state and never shortens expiry. Removal withdraws dependent maintained contributions without reversing independent committed consequences.
- Start and resume consequences gate admission. Mandatory pause/stop still closes operation if its exit consequence fails; the consequence charges nothing and applies nothing, with failure recorded through existing rule receipts. It cannot keep unfunded machinery running or introduce a retry scheduler.
- Share requirement evaluation, funding, exclusions and work receipts with existing execution code. Keep process-specific state separate from ability preparation/recovery; do not copy the ability runtime or introduce another scheduler.

## Machinery, work and funding

- Retain native electrical allocation, generator response, thermal protection, cooling reservations, flight physics and recipe accounting. Recipe processors retain ingredients, receiving claims, progress and batch commitments; process adapters own their operating lifecycle and funding.
- Produce frozen receipts from the committed native operation: accepted generated energy, heat actually removed, funded propulsion effort and native charging work, completed recipe work, or committed effect-only work. Propulsion work measures actuation, not resulting movement after collisions.
- Charge upkeep while operating, including idle periods. Charge work costs and run work effects only for completed work. Off or blocked processes perform no work; preserve existing proportional, pause and stop shortage policies.
- Extend the existing prepared allocation/payment path. Quotes and retries remain side-effect-free; commit inputs, native work and resulting effects once. Preserve exact fractional accounting and release rejected provisional reservations.
- Fund generator process inputs from resources available before current-tick generation. Generated power remains available to downstream consumers that tick. Reject self-funding and circular generator funding.
- Extend live modifiers to generation and cooling throughput. Express process rate and upkeep/work-cost scaling through existing typed properties and expressions. Preserve existing propulsion modifiers; capacities, topology and other physical configuration remain outside this stage.
- Add heat-add and heat-remove effect operations targeting existing native thermal owners. Heating follows native limits and protection rules; removal clamps at zero. Heat remains thermal state, and coolant is consumed only by explicitly authored costs or native cooling.

## Controls, authoring and continuation

- Preserve existing machinery controls and observations. Add named process controls and status through existing Console bindings, authority and knowledge rules. Human and AI users receive the same permitted frozen information.
- Show requested state, running/paused/off status, blocker, funded activity and completed work. Avoid shadow copies of native commands or physical counters.
- Add Workshop editing, verification, inheritance, overrides and export closure for processes and grants. Migrate maintained production definitions without adding production fuel or wear gameplay.
- Persist run/grant identities, intent, lifecycle state, credits and required fractional remainders. Rebuild derived aggregates and projections without replaying effects.
- Advance World and portable-session schemas together once from the implementation base—currently 29, therefore 30—and reject incompatible older saves.

## Delivery and verification

1. Establish catalogue definitions and shared execution support, proved by an effect-only process that changes a custom quantity.
2. Integrate producer funding and native generation, propulsion, cooling and processor adapters.
3. Complete temporary grants, thermal operations, controls, Workshop, production content and persistence.
4. Obtain independent correctness and architecture reviews, fix actionable findings, and commit verified logical steps.

Synthetic production-loader tests must cover:

- All four owner types, officer reassignment/travel, concurrent processes, exclusions, grant refresh/expiry, pause/resume and entry-cost behaviour.
- Mixed resource/quantity payments, fractional work, rollback, competing consumers and rejected reservations.
- Generator startup, self/circular-funding rejection, full reserves, ramps and same-tick downstream supply.
- Cooling with no heat or coolant; propulsion starvation, boost and impulse continuation; processor interruption and commitments.
- Thermal-operation bounds, lifecycle/work effects, permitted human/AI observations and consequential save/restore boundaries.

Run the full native and threaded WebAssembly gates, focused ordinary WebAssembly checks, production preflight and cross-platform continuation comparisons. Compare predecessor synthetic machinery results while normalizing only schema and documented ownership changes. Keep network timing checks separate from expensive compilation. Record executed evidence and update the migration roadmap. Manual GUI, accessibility, device and performance acceptance remain separate.
