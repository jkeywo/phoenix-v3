# Shared finite work — implementation and verification

Implementation base: `20b202e`, on `main`. The [approved plan](finite-work-migration-plan.md) and [authoring contract](finite-work-content.md) describe the ninth migration. This record will be finalized after the remaining gates.

## Implemented boundary

Finite effect-only jobs and native repair, treatment, stabilisation, facility recovery, docked energy/ammunition servicing and FTL preparation use shared process execution. Target episodes remain bounded entries in the existing process runtime, governed by their parent declaration or grant. Patient/fault progress, source-purchased structural work and physical delivery/FTL commitments retain native owners.

Native outcomes, work effects and input fees prepare and commit through the existing transaction. Completion consequences are once-per-run independent transactions; failure never rolls back completed native work. Static and temporary self-removal preserve committed receipts and cannot revive stopped state. Explicit restart retires old run credits and true stops reset finite state consistently. No new scheduler, wallet or ability runtime is introduced.

Typed structural/fault repair, treatment, stabilisation and recovery use shared native outcome helpers. Controls and scoped human/AI observations, Workshop editing/export, maintained content and strict World/portable-session schema 31 are included. Shared bay throughput rates use source-side facts; recipient-specific requirements and work costs use the concrete target.

## Verification in progress

Focused native process, Medical, craft and FTL suites pass across the documented integration runs, including final FTL readiness-after-consequence coverage. The full native/WebAssembly matrices remain to execute.

All predecessor physical comparisons report zero differences for Medical, craft and FTL (`build/finite-physical-comparison.log`). The same test-only capture code runs against the retained predecessor production source and the migrated source. It retains motion, native resources/heat, patient/team state and revisions, material inventories and credits, transfer IDs/routes/claims/progress, craft/bay state and away work. Normalization excludes schema and top-level execution records, and removes only new servicing attribution metadata from native transfer records, alongside the already documented neutral machinery metadata.

Independent correctness and architecture reviews found and drove fixes for concurrent target ownership, paid native credits, stop/restart state, funding policy, actual delivery, source withdrawal and fractional final work. Both final source reviews report PASS against the implementation base; immutable revision confirmation and full-gate results will be recorded below.

## Acceptance limits

No graphical, screen-reader, physical-device, six-display performance or Linux execution is claimed. Full destination-based FTL travel and launch/docking/away workflow redesign remain outside this migration. Nothing is pushed automatically.
