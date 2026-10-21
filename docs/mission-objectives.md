# Mission objectives and authoritative debriefs

Scenario objectives can describe a region crossing or a verified world condition. The simulation owns completion. Scenario scripts may activate, fail or cancel objectives, but cannot award completion. Condition objectives have no waypoint; their publication retains the normal recipient restrictions.

## Authoring

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

| Kind | Fields and references | Value |
| --- | --- | --- |
| `entity` | `field: lifecycle` | `active`, `destroyed`, `departed` |
| `compartment` | `field`: validated compartment ID | Fully restored and operational, Boolean |
| `away` | `operation`: mission ID; `field`: `phase`, `outcome`, `ever_deployed`, `recovered`, `lost` | Text or Boolean |
| `personnel` | `field`: `deployed`, `recovered`, `lost` | Integer count of distinct assets |
| `objective` | `operation`: objective definition; `field: status` | Objective status text in this situation |
| `event` | `event`: declared event, optional `subject` and typed `match` payload fields | Whether a matching event has occurred |

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

The rescue authors propulsion restoration, optional service restoration, party recovery and casualty escape as four condition objectives. The casualty carries a real FTL assembly. Its policy starts charging after the visiting operation completes, waits for visitors/transfers to clear and requests departure. Comms explains its charging/waiting state through stable operation events.

The departure report grades full rescue, partial withdrawal or failure through authored rules. It includes actual restoration and casualty/personnel disposition. No rescue identity, dialogue or grade rule is embedded in the runtime.

## Continuation and validation

World remains schema 13. Scenario continuation version 2 accepts preceding version 1 with the same content identity; away version 3 accepts versions 1/2 with derived available deployment history and empty unavailable history. New ledgers start empty when absent. Versionless frozen departure reports remain unchanged. Existing required objective geometry fields can be read against the new optional geometry contract without accepting unrelated schema changes.

`system.missions` uses independent synthetic loader fixtures for typed conditions, Missing negation, failure precedence, latched completion, report privacy and freezing, loss during transport phases, save continuation, AI charging/power refusal and simultaneous departures. `scripts/check.ps1` runs the native/WASM mission trace comparison. The explicit `phoenix_mission_smoke` target exercises shipped success, partial and loss branches graphically; it is excluded from CTest. Execution receipts are recorded separately from these contracts.
