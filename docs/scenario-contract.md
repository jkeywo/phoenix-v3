# Scenario state, spawns and objectives

Custom scenario `record_bindings` use the same typed schema, field, scope and subject metadata as System trees. A main tree or reusable situation selecting them must declare `bindings.actor` and `bindings.knowledge_console`, the Console ID on that actor's ship. Static actors and allowed slot hulls must contain that Console with reader roles permitting every selected schema. Spawned handles resolve against their actual hull at runtime; an unresolved handle or unsupported reader supplies no evidence.

At each tick the authoritative adapter requests those explicit readers, and World copies only their permitted Console/ship record projections before any scenario controller runs. Each independent situation receives an immutable snapshot. Field types, subject identity, publication permission, stale evidence and withdrawn stream versions are checked before filling custom observation slots. Missing evidence stays Missing. These snapshots never expose private memory or arbitrary World data. Reader selectors and tree continuations are saved; transient snapshots are reconstructed from the authoritative knowledge state on the next tick after restore.

`phoenix.scenario` compiles map authoring into typed data and holds authoritative scenario effects. The shared state-tree evaluator selects actions; this domain admits them and records their results. World construction, motion, permissions and published knowledge remain production integration responsibilities.

## Authored definitions

Regions live in `map.regions` (or `map.scenario.regions` when the top-level field is absent). Coordinates and dimensions are integer millimetres. A rectangle uses centre and half extents; boundaries count as inside.

```json
{
  "regions": [
    { "id": "arrival", "shape": "circle", "x_mm": 10000, "y_mm": 0, "radius_mm": 500 },
    { "id": "gate", "shape": "rectangle", "x_mm": 20000, "y_mm": 0, "half_width_mm": 1000, "half_height_mm": 2000 }
  ],
  "scenario": {
    "tree": "training-route",
    "bindings": { "actor": { "slot": "player" } },
    "parameters": { "traffic_enabled": true, "traffic_count": 2 },
    "spawn_points": [{ "id": "traffic-entry", "x_mm": 15000, "y_mm": 4000 }],
    "spawn_groups": [
      {
        "id": "traffic",
        "entities": [
          { "definition": "traffic-hull", "x_mm": 0, "y_mm": 0, "heading_mdeg": 180000 },
          { "definition": "traffic-hull", "x_mm": 1000, "y_mm": 0, "heading_mdeg": 180000 }
        ]
      }
    ],
    "objectives": [
      {
        "id": "reach-arrival",
        "type": "reach_region",
        "actor": { "slot": "player" },
        "region": "arrival",
        "recipients": { "ship": { "slot": "player" } },
        "title_key": "objective.reach-arrival.title",
        "description_key": "objective.reach-arrival.description"
      }
    ],
    "events": ["traffic-ready"],
    "publications": ["scenario-advisory"],
    "situations": [
      {
        "id": "traffic-watch",
        "tree": "traffic-watch-tree",
        "parameters": { "traffic_count": 3 },
        "bindings": { "event": "traffic-ready" },
        "start": false
      }
    ],
    "limits": {
      "spawn_per_tick": 64,
      "spawned_population": 1024,
      "instances": 128,
      "actions": 65536,
      "objectives": 4096,
      "events": 16384
    }
  }
}
```

The loader supplies verified entity-definition IDs and ship-slot names to `compile_scenario`. Missing entity/region/slot references fail before play. The outer loader verifies the selected tree and translation keys. Scenario fields are compiled once; numeric simulation never dispatches JSON instructions.

An actor is exactly one of `{ "entity": 42 }`, `{ "slot": "player" }` or `{ "handle": "spawned-courier" }`. A handle resolves within the situation instance that owns it. A single-actor operation requires exactly one entity in that handle; a group is never silently reduced to its first member. Console-scoped recipients add `"consoles": ["helm", "engineering"]`; omitted/empty Consoles mean shipwide publication. The integration layer verifies actual Console addresses and permissions when resolving publication.

Spawn placements are absolute for entity requests and relative to the supplied offset for group requests. Named spawn points are compiled for tree/editor binding. Coordinates are bounded to ±1,000,000,000 mm; rectangle corners must also fit. Positive circle radius and rectangle dimensions are required. These explicit bounds permit exact portable integer geometry on MSVC and WASM.

## Situation instances and actions

Each `create_instance` has independent parameters, entity/objective handles and timers. Parameter overrides must match declared names and scalar types. Cross-instance coordination reads declared events, never another instance's private tree memory.

Every side effect carries its situation instance, local activation/task and complete reference-call path with ancestor activations. The domain journals its typed result and argument fingerprint. Repeating the same action returns the recorded pending/success/failure result; reusing its token with different arguments is an error. A later activation uses a new token and may intentionally perform another spawn/objective action with a new handle. Tree-reference integration must pass this complete identity so parallel calls and later caller activations remain independent.

Spawning is a transaction boundary:

1. `spawn_entity`/`spawn_group` validates definitions, positions, handle availability and budgets; it returns a pending request or an explicit failed result.
2. World processes `pending_spawns` in identity order through its ordinary entity-construction path, with an atomic whole-group commit.
3. World calls `confirm_spawn` with the actual allocated IDs. Only then does the named entity handle become available. Construction refusal confirms no IDs and a diagnostic reason.

The domain never manufactures a physical spawn, a player claim, fleet membership or a quorum vote. Root World owns entity allocation and persists its allocator. Scenario owns request/objective/event IDs and persists its allocator separately. The spawn population budget accounts for confirmed scenario entities and pending requests. Accepted requests consume the current tick's attempt budget even when later physical construction fails. A new tick resets that budget; repeated `begin_tick` at the same tick does not.

Objective instances progress from Pending to Active and then Completed, Failed or Cancelled. Only executed geometry can produce Completed. Scenario actions can activate a pending objective or fail/cancel a nonterminal objective; they cannot command successful physical completion. Every instance binds its definition, assigned actor, declared recipients and stable result handle. Adding an active objective immediately checks the actor's current centre, so starting inside completes without a dwell period.

Completion stores actor identity and simulation tick exactly once. Publishing status and granting completion credit are separate acknowledgements. `pending_publications` remains durable until actual permitted delivery is acknowledged; credit requires a completed objective. An unsatisfied recipient binding must remain pending or report a failure at the integration boundary, never widen scope.

Declared events are append-only records with source instance, time, typed scalar payload and identity. Readers select events after their own cursor. Publication effects similarly require a declared record name and explicit recipients. Finishing marks only that situation instance; existing machinery, spawned actors and physical objectives continue normally.

## Tick and recovery contract

### Repeating objective sequences

`scenario.objective_sequences` contains `{ "id": "route", "objectives": ["reach-a", "reach-b"], "repeat": true }` definitions. Lists are nonempty and ordered; their objective definitions must share an actor and recipient scope. `start_objective_sequence(sequence, actor, handle)` starts one sequence using an explicit assigned actor and a stable result handle. Workshop exposes the list and repeat option alongside objective definitions.

Each activation owns one reusable objective slot. It stores the current definition, zero-based `sequence_index` and `cycle`. Completed legs advance at a later tick only after completion publication and credit are acknowledged and no old-leg publications remain pending. Physical movement still supplies completion; resetting a slot clears its previous completion evidence. A non-repeating sequence stops on its last completed leg; failure or cancellation stops progression. Repetition does not allocate additional objectives, situations or tree-action receipts. Save/resume retains the slot, cycle, current leg and pending publications; old saves without sequence fields retain ordinary objective behavior.

World calls `begin_tick` with committed ship-slot bindings and actual positions before evaluating scenario trees. It then admits scenario actions, constructs pending entities, and runs machinery/movement. `evaluate` receives all actual executed movement segments, including successive collision-resolved pieces; stationary positions are checked too. Centre entry, boundary contact and a complete high-speed crossing all count. No speed or dwell test applies. Terminal objectives are never re-completed.

Geometry uses signed integer dot/cross products and portable exact two-limb multiplication for circle/segment comparisons. It uses no platform floating point, libm or per-tick JSON parsing.

`save` includes situation parameters/handles/timers, objectives/evidence/credit, spawn roster and pending requests, action results, declared events, undelivered publications, world bindings, tick budgets and allocator continuation. `restore` validates a content signature and reconstructs transactionally into a temporary runtime. It checks references, typed parameters, scopes, identities, action/result links, time ordering and configured bounds before replacing live state. World must reconstruct and validate the saved dynamic physical roster separately before continuing.

Restore does not re-run an entry action, synthesize a spawn or announce an acknowledged publication. A pending construction saved before confirmation remains pending; a saved successful construction retains its handle and recorded action. Client projections expose only explicitly resolved recipient records and do not include hidden trees, future spawns or scenario-private memory.

## Production tree adapter

`phoenix.gameplay_scenario` binds cached production definitions to the shared contextual tree evaluator. `GameplayScenarioBridge::load` creates the selected main scenario and validates reusable situation configurations. `begin_tick` receives actual World bodies and slot bindings; `step` snapshots all situation observations before admitting any situation's effects. World then processes pending construction through its normal ECS path, calls `confirm_spawn` with actual identities, runs motion and supplies the executed segments to `evaluate`.

The `bindings` object selects an optional `actor`, objective result handle, region and event. With no objective selector, a situation observes its latest objective. With no region selector, it uses that objective's destination. This supports a tree adding a second objective after the first completes without leaving its selector fixed on the first handle. A selected event is read through the situation's independent cursor; omitting the selector receives declared events in identity order.

Tree observations include `self.entity`, authoritative scenario motion, objective identity/status/destination, scenario elapsed/finished state, delivered events and typed action receipts. They are scenario-context data, never an implicit System AI truth feed. Receipts distinguish pending work from successful or failed admission; `result.entity` is available for a successful singular spawn and `result.objective` for objective creation. An effect's result becomes an observation at a subsequent evaluation boundary, not earlier in the evaluation that requested it.

Supported scenario actions use the shared vocabulary: `spawn_entity`, `spawn_group`, `add_objective`, `set_objective_status`, `publish_objective`, `publish_scenario_message`, `emit_scenario_event`, `finish_scenario` and `start_situation`. `set_objective_status` can activate a pending objective or fail/cancel a nonterminal objective; it explicitly refuses successful completion, which requires physical execution. `publish_objective` retains the objective's declared recipients and cannot grant completion. Event/message scalar payloads use the typed `amount`, `flag` and `detail` fields.

`scenario.situations` declares reusable configurations, rather than consuming running-instance identities. Each has an ID, selected scenario tree, scalar parameter overrides and optional selectors. `start: true` creates an initial instance (`instance_name`, or its configuration ID); otherwise `start_situation(configuration, name)` starts one deliberately. Different instances share compiled behavior while retaining independent memory, parameters, handles and event cursors. The configuration limit is 4096; `limits.instances` bounds retained situation instances, including terminal ones whose identities and outcomes remain available to the scenario. Tree parameters use the shared tree declarations and receive matching scenario parameter overrides at creation, including inherited declarations.

One bridge step commits controller continuations and admitted domain effects together. If an evaluator or bounded action journal refuses the tick, pending spawns/publications, identity allocation, tree one-shot markers and receipts all retain their pre-step state. Physical World construction occurs only after that commit. The bridge persists its controller continuations, typed receipts, event cursors and sampling context alongside domain state. Restore validates action-to-domain kinds, full identities, request handles and resolved program signatures before replacing live state.

The production content verifier checks selected scenario programs, literal action references, configuration IDs, parameter types, region/event selectors and supported recipient Consoles. Dynamically supplied references receive normal runtime admission and explicit failure results.

## Verification evidence

The synthetic scenario integration source exercises production pack loading and compiled tree/domain behavior: high-speed circle/rectangle crossings, starting inside, intentional repetition versus replay, independent situation instances, failed/limited spawning, restricted publication and continuation on both sides of spawn/objective boundaries. It also covers transactional journal refusal and malformed continuation/receipt rejection. A separate World integration source exercises actual ECS creation, admitted movement, scoped objective records and dynamic-roster save/resume through `World` public APIs. Shipped mission values are content verification inputs, not balance goldens. Test execution results belong in the implementation record; this document does not claim the physical-device or graphical acceptance journeys have run.

## Condition objectives and debrief rules

The [mission objective contract](mission-objectives.md) extends this runtime with compiled typed facts/conditions, persisted transition events and authoritative departure reports. Live publications retain recipient restrictions. Mission grading does not set competition outcomes or freeze physical simulation.
