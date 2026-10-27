# Machinery process authoring

Processes describe continuous work on an entity, fitted equipment, officer or combat compartment. They use the shared effects catalogue, typed rule expressions and funding transactions. Native machinery retains its existing physics, resource network, thermal and recipe accounting.

This is an authoring reference for the [machinery migration plan](machinery-process-migration-plan.md). Implementation validation and manual acceptance are recorded separately. See [custom property authoring](custom-properties-content.md) for property declarations and typed expressions, and [Controller authoring](controller-content.md) for channel ownership and permissions.

## Templates and declarations

Put process templates in the ordinary `effects` array. Ordinary inheritance and explicit mod overrides apply. A minimal generator process is:

```json
{
  "id": "example.generator-process",
  "kind": "process",
  "label": "Power generation",
  "family": "machinery.generation",
  "native": "generation",
  "requested": true,
  "shortage": "proportional"
}
```

`native` defaults to `effects`; other choices are `generation`, `propulsion`, `cooling` and `processor`. `label` defaults to the template ID. `family` is an optional identifier for shared completed-work queries. `requested` defaults to true and supplies initial intent when a declaration does not override it. `shortage` defaults to `pause`; `stop` and `proportional` are also supported.

Declare named process slots on their owners:

```json
{
  "kind": "generator",
  "generator": "main-generator",
  "processes": {
    "generation": { "process": "example.generator-process" }
  }
}
```

The `processes` object is valid on entities, resolved equipment or equipment templates, `personnel.officers[]`, and `combat.decks[].compartments[]`. Each slot accepts `process`, optional Boolean `requested`, and optional `native_id`. Slot names are local to the owner and remain stable when equipment is placed through an assembly. There are at most 64 slots per owner and 4096 declarations per entity.

Native processes require compatible equipment. Entity, officer and compartment processes must use `native: effects`. Native IDs are resolved from the final fitted equipment:

| Mechanism  | Compatible owner                                                        | Inferred ID                  |
| ---------- | ----------------------------------------------------------------------- | ---------------------------- |
| generation | generator equipment                                                     | its `generator` reference    |
| cooling    | coolant equipment                                                       | its `cooling_link` reference |
| propulsion | engine, steering, lateral, boost or impulse equipment                   | fitted equipment ID          |
| processor  | equipment with a nested processor, or referenced by an entity processor | native processor ID          |

Omit `native_id` when the fit supplies exactly one compatible mechanism. Set it explicitly to choose among multiple processors on the same equipment. It must identify a mechanism belonging to that owner; it cannot redirect work to another fitting. Explicit local IDs inside assemblies are rewritten with the assembly namespace. Effect-only processes omit `native_id`.

The compiler freezes these native capabilities for every fitting. Static declarations, temporary grants and restored processes use the same resolver. Two live processes cannot drive the same native mechanism. Multiple effect-only processes, or processes using distinct mechanisms, can coexist.

## Lifecycle, conditions and exclusion

An optional `requirements` field is a shared Boolean expression. `rate`, `upkeep_scale` and `work_scale` are shared integer expressions with unit `permille`; absent scales are 1000. They can read the owner's declared properties and ordinary permitted facts. For example:

```json
{
  "requirements": {
    "op": "all",
    "args": [
      { "fact": "owner.present" },
      { "fact": "owner.healthy" },
      { "fact": "owner.on_duty" }
    ]
  },
  "rate": { "value": 1000, "unit": "permille" },
  "upkeep_scale": { "value": 1000, "unit": "permille" },
  "work_scale": { "value": 1000, "unit": "permille" },
  "exclusions": [{ "id": "example.exclusive-task", "scope": "source" }]
}
```

The officer facts above are appropriate to an officer-owned process. Requirements that cannot resolve their facts do not authorize work. Exclusion groups are distinct source-scoped identifiers; this process schema does not accept an apparatus exclusion scope.

`start_effect`, `pause_effect`, `resume_effect` and `stop_effect` reference existing `kind: operations` templates. An admitted run pays entry once. Temporary blockers pause that run, preserving requested intent and fractional work/funding. Resuming does not repay entry. Explicit stop, expiry, removal or termination ends it; a later admission starts a new run and repays entry. Native equipment master enable and safety controls remain independent gates on process intent.

Start and resume consequences are admission gates: their costs must be available before the process can operate. Generator entry, upkeep, work and admission consequences use pre-generation inputs, including expanded nested stop costs. A mandatory pause or termination still occurs if its authored exit consequence cannot be paid. That consequence fails atomically, charging nothing and applying nothing; it does not keep unfunded machinery running or schedule an automatic retry. A later lifecycle edge can attempt its own consequence. Work damage uses the existing deferred damage queue, so it cannot retroactively change the operation that produced it.

A process or process-grant template may also contain ordinary `rules` bindings. These use the existing shared reaction compiler and maintained-source lifetime rules.

## Costs and actual work

The three cost lists use the existing effect-cost selectors and payment machinery:

```json
{
  "entry_costs": [
    { "payer": "owner", "property": "example.charges", "amount": 1 }
  ],
  "upkeep_costs": [
    { "payer": "owner", "store": "service-battery", "resource": "base.energy", "per_second": 2 }
  ],
  "work_costs": [
    { "payer": "owner", "property": "example.wear-budget", "per_work": 1 }
  ]
}
```

The property IDs, store names and resource IDs in this example must exist in the content set. Property costs require an explicitly declared stored quantity. Each list may be omitted or empty. Amounts are constant nonnegative integers; typed `upkeep_scale` and `work_scale` expressions scale their respective bundles. Entry uses `amount`, upkeep uses `per_second`, and actual work uses `per_work`.

Upkeep applies while the admitted process runs, including native machinery waiting idle. Per-work charges apply only to actual completed native work. A request, funded intention or installed process alone produces no work receipt. Existing native energy and recipe costs remain authoritative; authoring process costs adds extra inputs rather than replacing native accounting automatically.

| Mechanism  | `event.work` unit   | Raw units per chargeable whole work unit |
| ---------- | ------------------- | ---------------------------------------- |
| effects    | `work`              | 1000                                     |
| generation | `energy`            | 1                                        |
| cooling    | `heat`              | 1                                        |
| propulsion | `effort_milliticks` | 1000                                     |
| processor  | `work_milliunits`   | 1000                                     |

`work_unit` is optional authoring metadata, but if supplied must equal this fixed unit. The derived `work_divisor` is not an authored field. Propulsion aggregates actual funded native effort/charging work; processors report their existing progress units. `rate` scales throughput and does not invent a second native progress counter.

Fractional payments carry through the existing funding credits. Generator inputs must be available before its generation; its new output cannot fund itself or a same-tick circular generation dependency. Downstream consumers can use committed generation through the ordinary network ordering.

## Work effects and typed quantities

`work_effect` references an operations template. `work_values` uses the same operation-index mapping as rule `values`. Only this completed-work evaluation can read `event.work` and `event.activity`; requirements and the three scaling expressions cannot read them. `event.activity` is a permille observation.

For an effect-only process, the following grants two charges per whole work unit:

```json
{
  "effects": [
    {
      "id": "example.work-output",
      "kind": "operations",
      "operations": [
        {
          "kind": "property_add",
          "target": "owner",
          "property": "example.charges",
          "amount": 0,
          "saturate": true
        }
      ]
    },
    {
      "id": "example.effect-process",
      "kind": "process",
      "native": "effects",
      "work_effect": "example.work-output",
      "work_values": {
        "0": {
          "amount": {
            "op": "ratio",
            "args": [
              { "fact": "event.work" },
              { "value": 1000, "unit": "work" },
              { "value": 2, "unit": "charges" }
            ]
          }
        }
      }
    }
  ]
}
```

The owner must declare a quantity `example.charges` with unit `charges`. The ratio converts the process work unit into the operation's required quantity unit.

Costs and receipts retain raw fractional work. Work effects accumulate partial work until a whole quantum completes: raw work 500 followed by 500 supplies one batch with `event.work = 1000`. Multiple completed quanta are evaluated as one batch, so quantitative output must scale explicitly from `event.work`. Computed values are not scaled a second time by activity. A fixed instantaneous operation, such as applying a timed modifier, executes once per nonempty batch; its duration is not multiplied by completed units. Use dynamic quantity values for proportional output.

## Temporary grants

A grant references a process template and optionally supplies its native ID and expiry:

```json
{
  "id": "example.temporary-task",
  "kind": "process_grant",
  "process": "example.effect-process",
  "duration_ticks": 200
}
```

`duration_ticks` defaults to zero, meaning no timed expiry. Apply and remove it through ordinary operations:

```json
[
  { "kind": "apply", "target": "owner", "grant": "temporary-task", "effect": "example.temporary-task" },
  { "kind": "remove", "target": "owner", "grant": "temporary-task" }
]
```

These are alternative operations for starting and ending the task, not a useful batch to execute together. Apply supports an exclusive `equipment`, `officer` or `compartment` selector; omitting selectors uses the context owner. The local `grant` names the temporary process slot; the runtime also retains its distinct application and generation identity. Refresh and removal follow shared source lineage. Native grants must resolve compatible fitted equipment before installation/payment can commit, including when the fitting is currently disabled.

## Controls and observation

Explicit owner endpoints expose named process intent:

```json
{
  "process_controls": {
    "service-task": {
      "source": "equipment",
      "owner": "service-unit",
      "process": "temporary-task",
      "controller": "engineering-controller",
      "enabled": "service-task-enabled"
    }
  }
}
```

`source` accepts `equipment` (default), `officer`, `compartment` or `entity`; entity endpoints omit `owner`. `process` is a local static slot or a temporary grant slot. The Controller channel must be `intent_boolean`, setting semantics, range 0..1. For a static slot its initial value must match declared `requested`. A channel cannot be shared with another process endpoint or an ability execution/planning endpoint. Equipment endpoints require the Controller Console's equipment feed; officer endpoints require the matching personnel Station.

Controller trees declare `process_bindings: ["service-task"]`. This exposes `process.service-task.phase` (text), `requested`, `running`, `paused`, `blocked` (Booleans), and `activity`, `work` (integers). Phases are `off`, `running` and `paused`. `process_property_bindings: [{"endpoint":"service-task","property":"example.charges"}]` adds the typed `process.service-task.property.example.charges` field. Template `observations` lists permitted property IDs for process observation. Human and AI readers use the same permitted projection and Controller authority; authoring an observation does not confer control over its owner.

## Heat and native modifiers

Heat remains in the native thermal network. Operations can add or remove it directly:

```json
[
  { "kind": "heat_add", "target": "owner", "equipment": "$owner", "amount": 10 },
  { "kind": "heat_remove", "target": "owner", "store": "thermal-reserve", "amount": 5 }
]
```

`amount` is a nonnegative integer with typed unit `heat` when supplied by rule/work values. Select a native reserve by `store`, or by an equipment owner/resource binding. If both are supplied they must identify the same native reserve. An entity alone has no implicit thermal reserve. Removal clamps at zero; adding heat follows native limits and protection. These operations participate in the prepared effect transaction alongside costs and other outputs.

Native property modifiers now include `generation` (unit `energy_per_second`, generator equipment) and `cooling` (unit `heat_per_second`, coolant equipment). Use the existing modifier contribution schema. These adjust native throughput through the common property path while preserving native controls, thermal behaviour and resource accounting.
