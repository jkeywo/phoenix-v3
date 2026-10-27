# Finite job authoring

Finite jobs extend [machinery processes](machinery-process-content.md). They use the same catalogue templates, requirements, exclusions, costs, grants and process controls. World and portable sessions use schema 31; older sessions require a new game.

## Effect-only finite work

```json
{
  "id": "example.job",
  "kind": "process",
  "native": "effects",
  "required_work": 2500,
  "completion_effect": "example.finished"
}
```

`required_work` is a positive integer in the process's internal work units. At the default rate an effect-only process commits 1000 units per tick. The final tick is capped to remaining work; its fractional input costs and work effect are accounted through the existing shared work path. `completion_effect` references an operations template. It is supported by finite jobs and native finite adapters, not ordinary continuous machinery.

Completion is once per run. A failed completion consequence commits none of its fees or operations, records failure, and leaves completed work complete. It does not retry on subsequent ticks or restoration. Starting a completed effect-only job creates a new paid run. Temporary blocking preserves the run; stopping does not refund inputs or reverse committed effects.

## Native adapters

| Native kind            | Owner and binding                                                  | Authoritative work                         |
| ---------------------- | ------------------------------------------------------------------ | ------------------------------------------ |
| `repair`               | Repair-capable team equipment                                      | Native structural and fault work           |
| `medical`              | Medical-capable team equipment                                     | Native treatment and stabilisation         |
| `recovery`             | Entity, `native_id` names its facility                             | Native patient recovery                    |
| `servicing_energy`     | Shuttle-bay equipment                                              | Energy delivered to docked craft           |
| `servicing_ammunition` | Shuttle-bay equipment, `native_id` names its ammunition connection | Rounds delivered through native logistics  |
| `ftl`                  | FTL equipment                                                      | Actual charging and live reserve readiness |

Servicing energy and ammunition have separate work units. Dispatching a request is not work: its actual receiving commitment must settle. Existing native travel, triage, facility places, stock filters, transfer throughput, thermal protection and reserve accounting still apply to these jobs.

A servicing `rate` scales the single bay or ammunition-connection throughput budget shared by its docked recipients. Use source-owned facts such as `owner.property` or `source.property` for that rate; verification rejects recipient-dependent `target` and `subject` rate facts because the shared budget has no single recipient. Requirements, work-cost scaling and effects still evaluate against each actual recipient episode.

A facility or bay can have several target episodes in the existing process runtime. Episodes retain their target, run identity and credits when work is interleaved. The authored parent owns controls and grants; stopping or withdrawing it stops dependent episodes. Native adapters rearm for genuinely new demand. FTL ready maintenance remains live and readiness can be lost when its physical reserve depletes.

Existing native work supplies remain authored through their domain definitions. Their payment is prepared with shared job costs. Native patient/fault progress and purchased structural work retain their existing native owners across contributor or target changes.

## Direct native outcomes

Operations templates can use:

```json
{
  "id": "example.restore",
  "kind": "operations",
  "operations": [
    { "kind": "structural_repair", "target": "owner", "compartment": "engineering", "amount": 10 },
    { "kind": "fault_repair", "target": "owner", "equipment": "reactor", "fault": "pump" },
    { "kind": "treat", "target": "owner", "officer": "engineer" },
    { "kind": "stabilise", "target": "owner", "officer": "navigator" },
    { "kind": "recover", "target": "owner", "officer": "navigator" }
  ]
}
```

Structural amounts are hit points and clamp to capacity. Fault repair names a specific active, repairable fault. Treatment restores a minor-injured officer or an injured team (select a team with `equipment`). Stabilisation requires an unstabilised severe casualty; recovery requires a stabilised severe casualty. An invalid transition fails the whole operations transaction. Dead personnel and destroyed/departed ships cannot be restored through these operations.

Direct outcomes require no team travel or facility work. Native jobs still enforce those requirements. Shared outcome helpers keep both routes consistent; direct operations do not instantiate phantom teams or facility slots.

## Information and continuation

Job readouts add finite status, target, progress, required work, readiness and completion delivery to permitted process observations. A `driver` row represents the authored automatic-work declaration; target episodes have their own stable identity and a `parent` key. Driver controls select the declaration, and do not select an arbitrary patient or craft episode. Native patient/craft facts retain their existing Console scope. Remote hidden targets do not become visible through a job endpoint; withheld Controller facts remain unavailable. Existing team, bay and Navigator controls continue to operate normally.

Workshop edits finite process fields and direct operations through the existing Verify/Test/export path. Save restoration preserves actual native commitments and process episodes without replaying entry, work, completion or payments. Automated evidence is recorded separately from graphical, screen-reader, device and performance acceptance.
