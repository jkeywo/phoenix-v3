# Mission objectives and authoritative debriefs

Scenario objectives can describe a region crossing or a verified world condition. The simulation owns completion. Scenario scripts may activate, fail or cancel objectives, but cannot award completion. Condition objectives have no waypoint; their publication retains the normal recipient restrictions.

## Authoring

### Planned Captain Objectives list and priority

The [Captain design agreement](bridge-command.md#captain-design-agreement--23-october-2026) adds a new Objectives list GUI element over the ship's published objective list on its entity blackboard. Captain may select one active priority objective whenever any Controller aboard is effectively AI-operated, including delegated Controllers on a human-operated Console. The accepted selection belongs to that same ship-scoped objective list; Controller AI reads it and considers it only where relevant and feasible. It neither changes objective completion nor grants machinery authority or access to private scenario facts. The GUI, priority state and policy integration are planned, not implemented by the objective/debrief work described here.

### Scenario objective definitions

A map's `scenario` accepts `fact_bindings`, `event_bindings` and `reports`. Workshop exposes these alongside objective definitions. All display titles, explanations and fact labels are localization keys.

```json
{
  "fact_bindings": [
    {"id":"target-state","kind":"entity","subject":{"entity":1},"field":"lifecycle"}
  ],
  "objectives": [
    {"id":"escape","type":"condition","title_key":"Secure escape",
     "actor":{"slot":"crew"},"recipients":{"ship":{"slot":"crew"}},
     "complete_when":{"fact":"target-state","value":"departed"},
     "fail_when":{"fact":"target-state","value":"destroyed"}}
  ]
}
```

Facts use existing `entity`, `slot` or singular spawned `handle` selectors. An unresolved selector produces Missing, including beneath `not`. Comparisons reuse state-tree typed values: `eq` (default), `ne`, `lt`, `le`, `gt`, `ge`, `present`; ordering requires integers. `all`, `any` and `not` compose expressions. A decisive false in `all` or true in `any` resolves the expression; otherwise Missing propagates. Expressions compile once, with type, depth and node bounds.

| Kind          | Fields and references                                                                      | Value                                   |
| ------------- | ------------------------------------------------------------------------------------------ | --------------------------------------- |
| `entity`      | `field: lifecycle`                                                                         | `active`, `destroyed`, `departed`       |
| `compartment` | `field`: validated compartment ID                                                          | Fully restored and operational, Boolean |
| `away`        | `operation`: mission ID; `field`: `phase`, `outcome`, `ever_deployed`, `recovered`, `lost` | Text or Boolean                         |
| `personnel`   | `field`: `deployed`, `recovered`, `lost`                                                   | Integer count of distinct assets        |
| `objective`   | `operation`: objective definition; `field: status`                                         | Objective status text in this situation |
| `event`       | `event`: declared event, optional `subject` and typed `match` payload fields               | Whether a matching event has occurred   |

Completion and failure use the same authoritative boundary snapshot. Failure wins a tie; terminal status is latched. Reports can use latched objective-status facts while displaying current restoration facts separately. Region sequences remain region-only, and competition/race outcomes remain separate from mission grading.

## Events and personnel

The persisted transition ledger records source, subject, operation, tick and detail. Transitions include deployment, work completion, operation completion/failure, recovery, personnel loss, entity destruction/departure and objective changes. `event_bindings` map a transition `kind`, optional `subject`, `operation` and `detail` into a declared `event`. Mapped events carry typed source/subject/ship IDs, tick, kind, operation, detail and outcome. Existing scenario and conversation consumers retain independent event cursors. Persisted transition identities and state receipts prevent replay after restore.

Delivered personnel remain on the target during inbound displacement; outbound personnel remain aboard their source until delivery. Target destruction creates a terminal lost party only for personnel actually delivered. Lost dispositions survive cleared manifests and prevent reassignment, bonuses and repair work. Departure is never destruction. Recovery facts require actual prior deployment; an initially empty manifest cannot complete a recovery objective.

## Departure boundary and reports

Each report definition selects a participating `ship`, situation `instance` (default `main`), `title_key`, objective definition IDs and labelled facts. Ordered `rules` contain `when`, `result`, `title_key` and `explanation_key`. An explicit `fallback` with the same result text fields is required. The first matching rule wins.

Human and AI FTL requests share Controller ownership/generation and physical readiness checks. Accepted requests are committed as a batch before further physical simulation. All lifecycle changes and objective facts refresh before any accepted ship's report freezes, so simultaneous departures see the same world boundary.

The frozen report contains the selected objective statuses, current explanatory facts and participating personnel classified as recovered, still deployed or lost. Active/pending objectives remain unfinished. These facts are authoritative even if undiscovered during play, and are exposed only after that crew departs. They do not publish hidden facts into live Console knowledge. Maps without a report definition keep the legacy objective-list report.

Controller trees opt into local FTL observations/actions with `ftl_system`, naming their owned fitted drive. `ftl.ready`, `local.visitors` and `local.work_completed` supply local physical facts. `ftl.depart(system)` uses the ordinary admission path; it cannot grant charge, clear visitors or remove a ship directly. Authored policy decides when to wait for visitors. Navigator telemetry exposes visiting-party/transfer counts to operators.

## Rescue content

The rescue authors propulsion restoration, optional service restoration, party recovery and casualty escape as four condition objectives. The casualty carries a real FTL assembly. Its policy starts charging after the visiting operation completes, waits for visitors/transfers to clear and requests departure. Comms explains its charging/waiting state through stable operation and physical FTL-readiness events. Its reactor output is authored through the normal Controller channel, and the escape policy allows the readiness announcement to arrive before departure.

The departure report grades full rescue, partial withdrawal or failure through authored rules. It includes actual restoration and casualty/personnel disposition. No rescue identity, dialogue or grade rule is embedded in the runtime.

## Continuation and validation

World remains schema 13. Scenario continuation version 2 accepts preceding version 1 with the same content identity; away version 4 accepts versions 1/2/3, with derived available deployment history and empty unavailable history for records predating mission history. New ledgers start empty when absent. Versionless frozen departure reports remain unchanged. Existing required objective geometry fields can be read against the new optional geometry contract without accepting unrelated schema changes.

`system.missions` uses independent synthetic loader fixtures for typed conditions, Missing negation, failure precedence, latched completion, report privacy and freezing, loss during transport phases, save continuation, AI charging/power refusal and simultaneous departures. `scripts/check.ps1` runs the native/WASM mission trace comparison. The explicit `phoenix_mission_smoke` target now exercises the Courier and Cruiser delivery/rescue openings graphically; it is excluded from CTest. See [the current walkthrough](rescue-opening.md). The earlier rescue-only success, partial and loss modes are preserved in the dated receipt below. Execution receipts are recorded separately from these contracts.

## Delivery conditions and independent results

`inventory` facts select an entity/slot/handle, a receiving store in `field`, and a material ID in `operation`. They return the actual quantity in that store; reservations, launched craft and transfers still in progress do not count. `clock` with `field: tick` needs no subject and returns active simulation ticks. `entity` also supports `field: definition`. An `objective` fact can return `completed_tick` (an integer only after completion), preserving receipt time through save/resume and later deadlines.

A report can contain up to 16 independently evaluated `assessments`, each with an `id`, `title_key`, ordered `rules` and `fallback` in the same form as the primary result. These are frozen at departure and included in the bounded remote projection and accessible report UI. A delivery failure therefore need not replace a successful rescue result.

Conversation starts support `cancel_events`: declared scenario events which permanently prevent a start and expire any undelivered queued messages in an existing conversation. Delivered history remains. Receipt and expiry settle before queued traffic advances in the same simulation tick. `away.available` can bind an event to the actual delivery of destination clearance, allowing rescue objectives to remain hidden until Comms receives it.

Synthetic native checks cover partial/full physical cargo receipt, inclusive deadlines and expired cargo, independent report results, swept player-only approach triggering, queued-message suppression and exact continuation. Production data remains editable; these tests do not fix shipped quantities, geometry or timers.
