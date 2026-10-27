# Shared finite work — ninth migration stage

Approved 27 October 2026. Implementation base: `20b202e` on `main`, incorporating the machinery migration and its consolidation. Verification is recorded separately.

## Intended result

Extend existing process execution to repair, Medical, docked craft servicing and FTL preparation, with effect-only finite jobs. Preserve native physical rules, production tuning, Controller authority and native progress ownership. Do not introduce another scheduler, wallet or parallel ability runtime.

## Execution and ownership

- Effect-only jobs declare positive required work. Native adapters derive demand and completion from their physical target. Shared runs identify their concrete target and deliver completion once.
- Temporary blocking preserves intent, credits and native progress. Cancellation ends the run without refunds or reversal of completed consequences. Effect-only completion requires another start; automatic native adapters rearm only on genuinely new work demand.
- Patient and fault progress stays with the patient/fault, including paid commitments across team replacement. Servicing fractions stay with the bay. FTL readiness remains its actual reserve state.
- Completion consequences use the existing effect transaction exactly once. Failure is atomic, consumes no consequence fees, records a bounded failure and does not undo native work or schedule retries.
- Reuse catalogue templates, temporary grants, exclusions, requirements, fractional work funding and frozen actual-work receipts. Reject competing native drivers.

## Native boundaries

Repair retains diagnosis, access, routes, priorities and supplies onboard and away. Medical retains triage, escorts, facility capacity, treatment, stabilisation and recovery. Craft servicing retains throughput, stable craft ordering and pending transfer commitments; only delivered energy/rounds count as work, in distinct units. FTL retains charging losses, ready upkeep, passive discharge, thermal protection and recall. Departure is still a deliberate, revalidated native action with authoritative reports.

Direct typed structural/fault repair, treatment, stabilisation and recovery effects reuse native outcome helpers. They need no team travel or facility work, clamp structural repair, require valid clinical transitions and never revive dead personnel.

Launch/recovery sequencing, docking, away orchestration and destination-based FTL travel remain native workflows outside this conversion.

## Authoring and continuation

Preserve existing controls and add permitted finite status, target, progress/readiness, blocker and completion through shared human/AI projections. Extend Workshop verification/edit/export and migrate maintained content explicitly. No permanent native fallback runtime.

Persist identities, targets, completion delivery, credits and native commitments without replaying payment or completion on restore. World and portable-session schemas advance together from 30 to 31; older sessions require a new game.

## Delivery and verification

Implement effect-only finite execution; migrate repair/Medical and servicing/FTL; complete direct outcomes and presentation; independently review correctness and architecture and fix findings. Commit verified logical steps on main without pushing.

Use production-loader synthetic fixtures for pause/cancel/restart, target/contributor changes, grants/exclusions, exact and fractional payments, competing work, completion failure, restoration, clinical/fault bounds, actual deliveries and reserve readiness. Compare predecessor native physical checkpoints with narrowly documented metadata normalization. Run native and threaded WebAssembly gates, focused ordinary WebAssembly, production preflight and continuation comparisons. Network timing checks run without competing heavy builds. Graphical, accessibility, device and performance acceptance remain separately recorded manual evidence.
