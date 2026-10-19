# State-tree runtime contract

`phoenix.state_tree` is the shared compiled evaluator for System AI and scenario
progression. It depends on `phoenix.data`, with no ECS, renderer or UI dependency.
The production content loader supplies verified tree definitions, their reference
catalogue and a capability-filtered vocabulary registry. The simulation binds its
typed results to ordinary command admission or authorised scenario actions.

The module interface, value constructors and `state_tree_definitions.cpp`
compilation/resolution implementation belong to the shared definitions library.
Content verification and Workshop may link that library. `state_tree.cpp` contains
evaluation, interruption and continuation persistence and belongs only to the
authoritative simulation library. Exported declarations do not require clients or
Workshop to link the evaluator. A disposable Workshop test uses the normal
authoritative server/native launch path.

## Public API and authority

Use `phoenix::state_tree::compile(definition, registry, catalog)` at the load
boundary. Retain the returned `Program`; create an `Instance` for each controller
or scenario situation. `step(program, instance, observations, ticks)` returns
consolidated `outputs`, current-evaluation `intents` and `debug` details.
`ticks` is a positive simulation-tick increment, bounded at 1,000,000.

The registry is a stable numeric layout:

- `observations`: named `FieldSpec` values supplied in the same order to `step`.
- `intents`: named fields with defaults, reset at the start of each evaluation.
- `actions`: named `ActionSpec` descriptors compiled to numeric indices.

`Context::System` is bit 1 and `Context::Scenario` bit 2. Each field and action
declares its permitted context mask. The compiler refuses forbidden bindings.
The caller supplies only vocabulary and observations that the instance is allowed
to use; the evaluator cannot fetch world state. Unavailable observations use
`Type::Missing`, including slots belonging exclusively to another context.
Observations have their complete layout/type checked before evaluation.

Values are tagged boolean, signed integer, text or typed identity (`entity`,
`system`, `console`, `objective`, `region`, `message`). Identities are positive
integers up to signed 64-bit maximum. Integer units are declared by the registry
owner (for example ticks or fixed-point engineering units); there are no floating
point expressions or scripts. Typed action argument arrays can combine actor,
System, objective and message references with payload values. Text is bounded to
4096 bytes and never interpreted as executable policy.

An `ActionSpec` declares argument types, `effect`, optional conflict `channel`,
optional `target_argument` or composite `key_arguments`, and optional
`intent_field`/`value_argument` linkage.
Non-effect outputs on the same channel and target resolve in authored order:
the later output replaces the earlier one before the caller sees any physical
command. With no channel the action index is the channel; with no target the
channel is global to the evaluation. Targeted and untargeted writes are distinct.
Nonempty `key_arguments` supersede the legacy single `target_argument`; the full
ordered typed tuple identifies a command's target. This keeps allocations to two
different groups on one distributor independent while retaining later-write
precedence for repeated settings of the same group.
Linked intent fields update immediately so later tasks can read them.

Effects are returned once per task activation. The caller still admits
the effect through its authority boundary and publishes acceptance/failure back
as permitted next-tick observations. Marking emission does not claim physical
success. Effect identity is the owning controller/situation identity plus
`Output.call_path`, `call_activations`, `activation` and `task`. Ancestor activation
IDs distinguish later calls from the same reusable placement. Persist receipts with the
world transaction that persists the continuation. The evaluator never performs
the effect itself.

## Authoring schema

All execution-order-bearing lists are arrays. Objects are used for named
parameters and literal identities, never for ordering active tasks.

```json
{
  "context": "system",
  "initial": "operating",
  "budget": 4096,
  "parameters": {
    "target": {"type": "system", "default": {"type": "system", "id": 7}},
    "limit": {"type": "integer", "default": 12}
  },
  "arguments": {"limit": 15},
  "memory": [
    {"name": "last_limit", "type": "integer", "initial": 0}
  ],
  "states": [
    {
      "id": "operating",
      "initial": "normal",
      "entry": [],
      "running": [],
      "exit": [],
      "transitions": []
    },
    {
      "id": "normal",
      "parent": "operating",
      "running": [
        {"memory": "last_limit", "value": {"parameter": "limit"}},
        {"action": "set_output", "arguments": [
          {"parameter": "target"}, {"memory": "last_limit"}
        ]}
      ],
      "transitions": [
        {"target": "cooling", "reason": "thermal-limit", "conditions": [
          {"left": {"observation": "heat"}, "op": "ge", "right": 100}
        ]}
      ]
    },
    {
      "id": "cooling",
      "parent": "operating",
      "running": [
        {"action": "set_output", "arguments": [{"parameter": "target"}, 0]}
      ],
      "transitions": [
        {"target": "normal", "reason": "cool-enough", "conditions": [
          {"left": {"observation": "heat"}, "op": "lt", "right": 60}
        ]}
      ]
    }
  ]
}
```

The example vocabulary (`heat`, `set_output`) must be supplied by the production
registry; these names are not special evaluator commands.

Bindings are a literal, `{"parameter":"name"}`, `{"observation":"name"}`,
`{"intent":"name"}`, `{"memory":"name"}` or `{"elapsed":true}`.
Parameters are substituted at compilation. Memory initial values may use a
parameter binding. A missing input makes comparisons false and skips an action
or assignment with an actionable diagnostic; a skipped one-shot remains eligible.

Task forms are:

```json
{"memory":"private_field", "value":{"observation":"known_value"}}
{"intent":"desired_setting", "value":42}
{"action":"registered_action", "arguments":[42], "once":true}
{"call":"reusable-tree-id", "parameters":{"limit":{"parameter":"limit"}}}
```

Tasks run in entry/running/exit array order. Each task has exactly one operation.
Assignments and non-effect actions normally run each evaluation; `once:true`
limits them to one execution in that state activation. Effect actions require
once (the default); deliberate repetition reactivates the state. Calls cannot be
marked once: put continuing calls in `running` tasks.
Calling a subtree in entry evaluates it once on entry, retaining its child
continuation until interrupted by the caller's exit. Each call placement gets a
separate compiled child and private runtime instance. Calls share the permitted
observation/intent layout, not private memory. Earlier caller/child intent writes
are available to later caller/child tasks within that evaluation.

Conditions use `left`, `op`, `right`. Operators are `eq`, `ne`, `lt`, `le`, `gt`,
`ge`, and unary `present` (no `right`). Ordered comparison requires integers.
Conditions in a transition form ordered AND; alternative transitions express OR.
An empty condition array is unconditional. No random hidden choice is added.

Every compound state declares its direct initial child. Parent cycles and depths
over 64 are refused. The tree initial state is a root. Entry descends through
initial children. Each evaluation runs root-to-leaf tasks, advances active timers,
then checks transitions leaf-to-root in authored order. It takes at most one
transition per instance per evaluation. Transitions are external: targeting the
source or its descendants re-enters the source, resets its timer/one-shot markers
and interrupts its referenced decision instances. Other transitions preserve the
common active ancestor. Exit runs deepest-first; entry runs shallowest-first.
Newly entered running tasks wait until the next evaluation. Entry and exit tasks
can still issue ordered outputs on the transition evaluation.

## Inheritance and references

The catalogue maps stable tree IDs to source JSON. `extends` resolves a base tree.
Derived named parameters/arguments merge; explicitly supplied other fields replace
the inherited field. `overrides` is an ordered array of named-state patches:

```json
{
  "extends":"base-power-controller",
  "arguments":{"limit":20},
  "overrides":[
    {"state":"normal", "running":[
      {"call":"power-policy", "parameters":{"limit":{"parameter":"limit"}}}
    ]}
  ]
}
```

A patch must identify an existing state. Array fields replace the corresponding
array; there is no implicit object-key execution ordering. Derived `states` can
replace the full structure. The compiler verifies the resolved definition and
every referenced parameterisation. Unknown parameter arguments, mismatched types,
cross-context calls, missing references and inheritance/reference recursion are
errors. Inheritance/reference depth is at most 32; resolved compilation is bounded
to 8192 states across references, 1024 states per program, 8192 tasks per program
and 65536 tasks across the resolved reference graph.

## Continuation, interruption and diagnostics

`save(program, instance)` includes program signature/version, active frames with
timers and activation IDs, one-shot markers, typed private memory, child-call
continuations and the activation allocator. `load` refuses incompatible program
signatures, invalid active paths, malformed memory, unrelated child calls and
invalid one-shot markers. A signature includes the resolved source, parameter
arguments, registry layout and referenced programs. Existing timed-tree saves
remain the responsibility of their compatibility loader.

`interrupt` runs exit work, interrupts child decisions, clears the active path
and leaves the instance stopped. It does not cancel effects already returned or
physically completed. `reset` permits a fresh explicit activation while retaining
the root activation allocator. Save/restore
does not itself evaluate entry tasks or re-emit effects.

`Result.debug` exposes active state names, the last taken transition reason,
unavailable-input diagnostics and work consumed; pending typed outputs remain in
`Result.outputs` for caller admission/debug presentation. Workshop may inspect
these in a disposable instance; ordinary player projections must retain their
existing knowledge boundaries.

The evaluation budget is 1..65536 operations. Runtime work counts state entry,
tasks, output conflict checks, transition candidates and conditions across all reference calls. Evaluation
is transactional: budget/timer failure throws before committing the new
continuation or returning any effects. The caller must report the diagnostic and
use its authored/system failure policy rather than retrying a permanent runaway
indefinitely. Registry names and JSON are interpreted only at compilation or
serialization boundaries; the stepping path uses compiled indices and enums.

Synthetic production-loader integration should cover inherited/reference order,
independent memory, missing inputs, resource-command precedence, once-only effects,
interruption, branching/timers, invalid contexts/cycles, bounded failure and
uninterrupted-versus-resumed continuation. These tests establish evaluator
contracts without freezing shipped policy values.

## Supported gameplay vocabulary

`phoenix.gameplay_vocabulary` supplies `gameplay_tree_registry(Context)` and
`gameplay_tree_vocabulary()`. Both contexts retain identical numeric layouts;
context masks control which bindings/actions compile. The stable
`GameplayTreeObservation`, `GameplayTreeIntent` and `GameplayTreeAction` enums
correspond directly to registry indices. Consumers switch on enum indices, not
authoring strings. The metadata function returns ordered observations, intents
and action signatures with field units and argument names for Workshop.

The fixed observations include:

- Typed self entity/System/Console identities and simulation tick.
- Known enabled/requested/actual/available output; reserves, capacities and
  incoming/used/reserved power; heat/cooling/coolant and thermal/supply status.
- Permitted own position, velocity and heading; assisted/manual mode, boost,
  impulse progress and observed Match Speed state including staleness.
- One selected objective's identity, known status and permitted destination.
- One delivered message's typed identity/sender, schema, request key, semantic
  fields, evidence time, uncertainty and receive cursor.
- One associated action result's request key, success/reason and returned
  entity/objective/message/publication handle.
- Scenario-only elapsed time, declared event contract fields, selected region
  membership and ending status.

The caller selects the record/objective/message/result for each instance through
its compiled bindings and memory. Empty slots are `Missing`. Supplying a fixed
slot never grants access to an otherwise private record, hidden target motion or
another situation's memory. The result slots describe committed admission/output
results on a subsequent tick; they are not optimistic return values from `step`.

Additional Console/shared fields use the overloads accepting ordered
`GameplayTreeRecordBinding` descriptors. Each descriptor names a scope, schema,
field, optional declared subject parameter, value type and permitted context mask.
The loader resolves the subject/schema and validates read permission before
filling that appended numeric observation slot. No tree performs arbitrary
schema lookup or string-based board queries during evaluation. Scenario-scope
bindings require the scenario context. The metadata overload emits these same
bindings for inspection and typed pickers. The fixed prefix remains unchanged.

Supported action families are:

| Family       | Actions and semantics                                                                                                                                                                                                                                                                          |
| ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Flight       | Speed, heading, enabled, assisted/manual mode, signed longitudinal/lateral/steering permille, boost, impulse, selected-contact Match Speed and approved destination. All target an explicit fitted System and use ordinary admission.                                                          |
| Engineering  | Generator output; store charge/discharge limits; group allocation; link isolation; cooling allocation; thermal override and summary channel intent. Store/group/link/consumer/channel names resolve within the explicit owning System. Composite keys keep different subdivisions independent. |
| Coordination | Separate typed System, Console, station and ship destinations; explicit receive acknowledgement through a message; withdrawal by sender/request key. Sending uses a verified template and typed subject/amount/flag/detail fields, request key, delay, lifetime and urgency.                   |
| Publication  | Publish a verified publication definition from an explicit Console/subject, optionally live; stop an existing publication by ID. The definition determines permitted schema/field selection.                                                                                                   |
| Scenario     | Spawn definition at named point or explicit position/heading; spawn authored group; add objective for an explicit actor; publish objective; publish scenario message; emit declared event; finish with success/failure reason.                                                                 |

Flight/Engineering actions are intents, consolidated before ordinary machinery
admission. Coordination/publication/scenario actions are effects, emitted once per
activation. Scenario registries reject all System operating and Coordination
actions; System registries reject spawning, objective creation and scenario
ending. There are no role-name checks in these contracts.

Message templates declare their schema, mapping of the typed payload arguments
and localised presentation; arbitrary detail text cannot introduce schema fields
or execution authority. Lifetimes/delays/urgency and physical output limits are
validated at admission. A failed destination never becomes broadcast. Station and
publication identities use integer slots because they are not entity/System/
Console/objective/region/message identities. `receive_messages` acknowledges only
this instance's own delivered-message cursor; it cannot consume another AI's
evidence.

For spawn actions, an empty `spawn_point` selects the explicit transform;
otherwise the verified named point provides position and the supplied heading
still applies. Spawn group/objective definitions and publication/event templates
must resolve in the caller's compiled scenario. `handle` names let subsequent
compiled observation bindings select the resulting actor/objective. Objective
checking uses `objective.state` conditions; no tree action is allowed to invent
physical reach-region completion.

`start_situation(configuration, name)` starts a verified scenario situation
configuration with its declared tree, parameters and selectors under a distinct
instance name. It does not inherit the caller's private memory. Repeating the
effect requires a later task activation and a new valid instance name; ordinary
exactly-once effect receipts prevent replay from creating duplicates.
