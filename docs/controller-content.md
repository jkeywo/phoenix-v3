# Authoring Console Controllers

Player Consoles use schema-2 shared-widget layouts. Each logical widget owns one
Controller, an explicit set of equipment roles, one policy and its responsibility
levels. The same definition can be fitted to different ships or used several times
on one ship. See [the rewrite contract](control-widget-rewrite.md) and
[the architecture](controller-architecture.md).

## Shared definitions and instances

This fragment gives two engines one throttle. It omits the surrounding pack,
physical fittings and Console definition:

```json
{
  "control_widgets": [{
    "id": "example.drive", "schema": 1,
    "equipment": {"engines": {"multiple": true, "kinds": ["engine"]}},
    "commands": [{"id": "speed", "action": "speed_mm_s", "initial": 0, "consumers": ["$engines"]}],
    "data": {
      "enabled": {"binding": "system.engines.enabled", "label": "Enabled", "placement": "main", "required": true},
      "speed": {"binding": "command.speed", "label": "Requested speed", "placement": "details"}
    },
    "parts": ["main"],
    "levels": [
      {"id": "automated", "human": []},
      {"id": "manual", "human": ["speed"]}
    ]
  }],
  "entities": [{
    "id": "example.ship",
    "control_layout": {
      "schema": 2,
      "controllers": [{
        "id": "propulsion", "console": "helm", "widget": "example.drive",
        "equipment": {"engines": ["port", "starboard"]},
        "star_levels": {"1": "automated", "2": "manual", "3": "manual"},
        "data_placement": {"3": {"speed": "main"}}
      }],
      "profiles": [{"id": "simple", "stars": 1}, {"id": "middle", "stars": 2}, {"id": "detailed", "stars": 3}],
      "default_profile": "simple"
    }
  }]
}
```

Place it with a Console layout component `control/propulsion/main`, or
`control/propulsion/complete` for a reunited compact presentation. The compiler
checks every rating and layout variant for access to human commands, native
operations, required main readouts and Details. A definition without a policy
holds accepted settings; author a `tree`, role-bound inline `policy`, or the
existing `policy_template: "combat"` when it should make decisions.

Instances bind roles and map each rating independently. They cannot replace the
shared commands, operations or policy. Derive a library definition for an
intentional variation. An inline policy uses ordinary tree inheritance and `$role`
bindings for System observations. `repeat` with an inline `member_policy` expands
one equipment set into independent member channels and private continuations
under the same Controller, using the ordinary evaluator.

Each physical input has one owning channel. Settings persist until changed;
requests retain their identity, receipt and single-executor rules. Queued inputs
carry the Controller generation, so a stale input cannot survive a change of
operator or responsibility. Held thrust, steering and boost return to neutral on
handover. Physical telemetry remains separate from accepted intent.

## Responsibilities, native workflows and data

An Automated level has no human responsibilities and produces readouts. Other
levels list the complete human command and operation set. Levels retain earlier
responsibilities or explicitly `replaces` an assisted command with its detailed
commands. Ratings can share a level or select an Automated replacement at a later
rating. Unclaimed, disconnected and Away Consoles give all responsibilities to AI.

The same tree evaluates at every rating. AI attempts at human settings succeed
without mutation when the accepted setting already matches; other writes are
refused. Ordered `attempt` branches allow explicit same-tick fallback. Author the
rest of each branch for the setting actually accepted, including ammunition choice.
A failed or exhausted evaluation commits neither staged commands nor continuation.

Native workflow responsibilities live in the shared definition's `operations`,
whose values name equipment roles. They include Team destination and work,
Medical, personnel, Team selection and manifest transport, department contribution,
mission staffing/lifecycle/send/quantity, craft, logistics, docking, departure,
conversation, coordination, alert, stance and objectives. Allocation of one does
not grant another. Native actions carry the widget generation and their existing
domain revision. Physical mechanisms and their admission constraints are unchanged.

Named data uses `command.channel`, `system.role.field`, `weapon.role.field`,
`team.role.field`, `ftl.role.field`, `shield.role.field`, or a scalar
`board:/json/pointer` into the permitted gameplay projection. Main/Details/Hidden
placement resolves from definition, level, then instance per-star override,
independently of control allocation. Each datum needs a nonempty localisable label
of at most 256 bytes. Required state indicators remain on Main.
PDC definitions can put battery and ammunition on Main; missile definitions can
put actual loaded resource and quantity there. Visibility grants no extra evidence.

Named `part_bindings` divide commands, native operations and data between visual
parts while retaining the same state, generation and browsing context. They may
reuse an existing native `renderer`, declare setting `ranges`, filter button
`values`, and expose one shared Details entry. Lateral left/right parts can occupy
the radar gaps and reunite as `complete` in a compact layout. Button filters must
fit the bound Controller command, including fitted equipment limits and declared
choices. Boolean filters match the numeric command envelope (`false` = 0, `true` =
1); integers must fit signed 64-bit command values.

## Physical motion and initial heading

`motion_model: vector` preserves the simple engine's existing vector-seeking acceleration and separate steering. It supports engine, steering and radar mechanisms with speed, heading and enabled commands. Resource networks, manual axes, navigation guidance and other advanced physical inputs require inertial machinery. `motion_model: inertial` explicitly selects that machinery. Both models use the same Controller admission, profiles, command sections, observations and continuation; this is a physical capability choice, independent of human or AI operation.

An omitted `initial` on a heading channel uses the entity's spawn heading. An explicit integer instead requests that heading. A spawn heading outside the channel's authored range is refused; it is not silently clamped. Other channel defaults retain their documented typed values.

## Preferences shared with AI

### Star presentation and station selection

Profiles declare stars 1, 2 or 3. Widget instances map those ratings to their own
responsibility levels; there is no second human/AI allocation table. The lobby
and Console settings select the accepted profile. `station_complexity: true`, or
an explicit list of Station ids, groups selection for those Stations. Other
Stations retain their independent choice. Restore rejects contradictory selections
within a group. Fixed auxiliary profiles retain their authored rating.

Native responsibility follows operations rather than hidden star thresholds.
For example, a Team widget can grant `team_assignment` at one star while keeping
`team_work` and `medical` automated, and add detailed work at a later rating.
Conversations can remain human-controlled while the radio's readiness is automatic.

A weapon-target command may declare `shared_target: {"controller": "scanner", "channel": "target"}`. The source must be a sensor-target or weapon-target command on the same Console and cannot itself follow another source. The weapon follows this accepted target until its human operator explicitly overrides it. AI handover clears the override, including when a lower complexity profile takes over. The override is saved with the command state.

A Controller tree may opt into `complexity_observation: true` to read the integer `console.stars`. This lets ordinary authored policies distinguish workloads, such as defensive-only PDC operation at one star and automatic defensive/offensive selection at two stars.

An `intent_integer` or `intent_boolean` setting has `consumers: []`. It represents an accepted Console preference, not a physical actuator input. Author bounds and an initial value as for other settings. For example, a human-operated planning Controller can own `{"id":"power","action":"intent_integer","minimum":0,"maximum":1000,"initial":650,"consumers":[]}`. An AI-operated generator Controller reads it through a same-Console `command_reads` binding and writes its own physical generator-output channel. Both operators use the ordinary command admission, generation, receipt and continuation contracts.

Preference settings persist across handover. A hold fallback retains the last preference when nobody operates its Controller. Reading a preference never grants another Controller permission to overwrite it. Physical actions still require compatible consumers; preference actions cannot bind equipment. Generated controls expose these settings and Workshop uses the same action vocabulary.

## Navigation commands

`destination` and `follow_contact` are boolean settings with an atomic `arguments` tuple. A destination channel supplies `[x, y]` in millimetres; a follow channel supplies `[contact, separation, speed_limit]` in entity ID, millimetres and millimetres per second. Coordinates are bounded to +/-1,000,000,000; separation and speed must be positive and at most 1,000,000,000. A disabled follow setting may use contact zero; enabling requires a positive contact and cannot target the owning entity.

For example, `{"id":"navigation","action":"destination","initial":0,"arguments":[100000,50000],"consumers":["port","starboard","rudder"]}` declares one owned destination setting. Consumers must be compatible engines or steering mechanisms. Both the enabled value and the complete tuple are admitted, committed and saved together. Scalar commands refuse extra arguments. Generated accessible controls edit these same tuples; follow target choices come from permitted Console contacts.

A Controller tree binds the channel with `{"channel":"navigation","type":"boolean","action":"destination"}` and writes `write.navigation` with `[true,100000,50000]`. For follow, declare `action: follow_contact` and write `[true,contact,separation,speed_limit]`. The accepted enable value remains `command.navigation`; tuple fields are `command.navigation.x` and `.y`, or `.contact`, `.distance` and `.speed`. These reads use the same frozen command snapshot as scalar channels. Timed scalar policies cannot implicitly provide a navigation tuple.

Guidance runs in the physical execution path using the accepted setting and permitted contacts, independently of who wrote it. Heading and manual navigation controls cancel active navigation; unrelated enabled/boost controls do not. Speed changes cancel follow but preserve destination guidance, allowing a separate cruise-speed setting. Destination guidance preserves the existing steer-toward-point behavior; it does not add route planning or obstacle avoidance. Follow retains the existing powered pursuit and stale-contact behavior.

## Controller trees and observations

The content compiler builds Controller tree bindings and their registry together. Simulation setup resolves the bindings to entity-local sources; the tick reader uses their compiled destinations rather than reconstructing field offsets. These bindings are derived data, rebuilt with tree programs after Catalog edits and on restore. Registry ordering and saved Controller continuation retain their existing contracts.

Use `context: controller`. Declare `command_bindings` for the channels the tree owns; each supplies `command.<channel>`, an internal intent and `write.<channel>`. Declare `type: boolean` for boolean channels and `semantics: request` for request channels. Default types and semantics are integer and setting.

```json
{
  "id": "example.cruise",
  "context": "controller",
  "initial": "run",
  "command_bindings": [{"channel": "speed"}],
  "states": [
    {"id": "run", "running": [{"action": "write.speed", "arguments": [5000]}]}
  ]
}
```

This writes the same accepted throttle setting that a human control writes. It does not mutate an engine component. Physical consumption applies that channel to its compiled consumers. Consumers retain their own physical/resource constraints.

Available decision inputs include own position, velocity and heading; permitted record bindings; typed `system_bindings`; same-Console `command_reads`; and the addressed message inbox. A System binding names `name`, `system` and `field`, with an optional checked `type`. Availability comes from the fitted producer, so an engine without a resource store cannot supply `heat`. The System must also be in the Console's feed roster. A command read names `name`, `controller`, `channel` and `type`; reading another Controller's channel grants no write authority.

Each record binding also exposes `.available`, `.observed_tick`, `.received_tick`, `.source`, `.uncertainty`, `.kind`, `.withdrawn`, `.stale` `.revision` and `.subject`. Metadata describes the same permitted record selected for its value. When no live value remains, the latest matching stream's metadata explains its absence; withdrawal prevents falling back to an older value from that stream. With no matching record, `.available` is false and other metadata is absent.

Controller record bindings may use `subject: latest` to select the most recently published subject of that schema within the permitted board. All fields then read that subject's streams; a withdrawn latest subject does not silently switch back to an older objective. This supports scenario objectives whose subject is an objective identity, without privileged scenario reads.

`contact_bindings` select a same-Console Controller's `selected_contact` or `follow_contact` channel, for example `{"name":"tracked","controller":"navigation","channel":"follow"}`. They expose `tracked.available`, `.id`, `.x`, `.y`, `.vx`, `.vy`, `.heading`, `.speed`, `.observed_tick` and `.uncertainty`. Selection uses the frozen accepted command and reads only the Console's frozen contact feed. An undetected target has `available: false` and absent remaining fields. Current raw detection has zero uncertainty; this is not a sensor-estimation or tracking implementation. Human observations carry the same sample tick through local and remote projection.

Owned command bindings expose `.accepted`, `.revision`, `.consumed`, `.request_id` and `.pending`, alongside their values and any navigation tuple. `pending` distinguishes a newly committed command from its physical execution receipt; `accepted` describes the last consumption result. Human Console projections carry the same receipt fields. These are shared operational facts, separate from private tree continuation.

Decision inputs freeze before Controller evaluation. An earlier Controller's write becomes visible to other Controllers on the next tick. Private tree memory and continuation remain separate from Console observations and are never supplied to the GUI. Human and AI projections use the same operational observation producer.

## Reports and Coordination

A physical System's `observation_console` names the Console that owns its authored records and reporter producer roles. It defaults to the first Console when omitted. Set it explicitly for multi-Console fits: command subscribers and telemetry feeds do not implicitly transfer record ownership. Authored feeds/publications determine additional permitted readers.

Physical Systems can declare `reporters`, independently of AI trees:

```json
{
  "reporters": [
    {
      "id": "temperature",
      "field": "heat",
      "above": 500,
      "message": "example.hot",
      "recovery_message": "example.cooled",
      "debounce_ticks": 20,
      "recovery_ticks": 40,
      "minimum_gap_ticks": 100
    }
  ]
}
```

The enclosing fitting must actually produce `heat`. Both message templates must be permitted by the entity and physical observation owner. Reporter messages are Facts. Their dynamic bindings may use measured `amount`, threshold `flag` and System-name `detail` arguments, not AI memory or decisions. Healthy startup is silent; initial warnings obey debounce. Warning/recovery latches and reporting intervals survive handover and restoration.

A Controller destination is `{"kind":"controller","console":"helm","id":"propulsion"}`. It names the receiving Controller, not an engine. Explicit layouts reject physical System destinations with a migration diagnostic; Systems register as reporter sources, without AI memory or inbox cursors. At delivery the current operator determines human notification or AI handling. Trees declare permitted `message_templates`, send through `send.<template>` with amount/flag/detail arguments, read typed `message_bindings` plus `message.*` metadata, and acknowledge an available message through `acknowledge_message`. Human message controls use the same authorised template route.

Entity-wide messages use destination kind `ship` in the current serialized vocabulary. Important AI-to-AI messages (urgency at least 2) also publish if their recipient remains AI at delivery. Publication requires a publishable schema and fields; dynamic public presentation parameters must bind a public payload value. Public copies retain identity and evidence but give spectators no action or acknowledgement authority. Ordinary targeted messages stay private.

## Compatibility and verification

Workshop's **Console Controllers and complexity** inspector edits shared-widget
instances, equipment role bindings, star-to-level mappings and per-item data
placement. The shared-widget library edits commands, policies, responsibility
levels and named visual parts. The layout editor places parts and the rendered
ship-context preview checks each rating. Export/reopen retains authored bindings;
compiled per-instance policies are regenerated.

Controller trees expose command, System observation, peer-command and message bindings. Their task palette comes from the production registry for the resolved tree, including inherited bindings. Inherited binding groups remain read-only until explicitly overridden; removing a local group restores inheritance. System Properties expose factual reporters, and Coordination destinations include Console/Controller addresses.

Isolated Test feedback identifies each Controller by entity, Console and Controller ID, with its effective operator, generation, active states, transition reason, diagnostics and bounded command receipts. This debugging view is separate from player observations and does not export private tree memory.

World and portable session saves use schema 34 with per-channel and native-operation
responsibilities and Controller continuation. Older saves and obsolete authority
fields are refused transactionally; begin a new session. Authenticated command
transport carries Controller, channel, generation, request identity and typed
arguments. Native operations also carry their existing domain revision. Source
Controller layouts before schema 2 are rejected; no runtime ownership converter
or manual-workload fallback remains.

Preference channels (`intent_integer` and `intent_boolean`) may declare `choices: [{"value":1000,"label_key":"Cruise policy"},{"value":3000,"label_key":"Fast policy"}]`. Choices are unique, in range, limited to 64 entries and include the initial value. Both human and AI admission enforce the list. Labels use ordinary localisation; Workshop can edit them. Omit choices for a continuous bounded setting. An optional channel `label_key` labels its generated controls.

Controller trees may declare `publication_templates: ["drive-request"]` to expose `publish.drive-request()`. The named publication must belong to the owning Console and only republishes its permitted producer evidence. It cannot create facts. `stop_publication(integer_handle)` ends an owned Console publication; `withdraw_request(text_key)` withdraws that Controller's request. `result.available`, `result.success`, `result.id`, `result.publication`, `result.message` and `result.reason` expose the last action result to private tree logic. Publication handles are integers; message handles have the Message type. Capture handles in private memory when later actions would replace the result. Results, memory and effect receipts survive saves; interruption uses the same scoped cleanup actions.

Use the production Catalog loader to validate bindings, then synthetic integration fixtures to verify behavior. `tests/support/controller_fixture.hpp` and `tests/system/controller_runtime.cpp` exercise the actual load, authority, command, physical, messaging and restore paths. Shipped ship tuning is not a test golden. The refactor plan remains the source for unimplemented migration and acceptance work.

For the explicitly triggered shared-widget graphical journey:

```powershell
python tests/manual/control_widgets_ui.py
./build/widget-finish-native/Release/phoenix_control_widgets_smoke.exe build/control-widgets-ui.zip
```

It exercises the real Console and Workshop renderer, readouts and Details,
command and range admission, independent widget state, per-star data placement,
split parts and compact reunion. Production layouts can be inspected with:

```powershell
./build/widget-finish-native/Release/phoenix_control_widgets_smoke.exe --inspect build/widget-finish-native/base.zip base.rescue-under-threat courier base.rescue-courier helm build/courier-widgets
```

These are manually triggered smoke runs. Physical touch/gamepad, screen-reader
usability and simultaneous six-display performance require their own hardware
acceptance checks.

## Cyclic policies

All Controller decisions use the hierarchical evaluator. The former object-keyed timed tree format is rejected. For a cycle, author states with `running` actions marked `once: true` and transitions comparing `{"elapsed": true}` against the duration using `ge`. Running actions execute on the first tick in each state; the next state's outputs start the following tick, including for one-tick states. Use explicit owned `command_bindings` and `write.<channel>` actions. Human-to-AI handover restarts at the initial state while normal Controller command preservation applies.

World and portable saves before schema 34 require a new session. Existing save files are preserved; no continuation conversion is attempted.

## Ship navigation assessment

The ship computes obstacle clearance and lateral-avoidance evidence once per fixed tick, before Controller trees run. This close-in lookout reads physical world geometry and motion directly, as a crew watching the Viewscreen would; radar range, fog and Console feeds do not limit it. The entity blackboard publishes `navigation` with `observed_tick`, `blocked`, `left_heading`, `lateral_thrust` and `lateral_safe`. Every Console and Controller on that ship can read this shared assessment without receiving another Console's private contact list or telemetry.

Existing `navigation.blocked`, `navigation.left_heading`, `navigation.lateral_thrust` and `navigation.lateral_safe` tree observations read that blackboard. Assessment never writes machinery commands. Lateral recommendations consider AI-owned lateral machinery; human-owned channels remain under their operator's authority. Saves retain the latest blackboard assessment for consistent checkpoint presentation; older saves without it start with an empty assessment. The ship recomputes it before the next Controller step after load/resume.

## Shared action execution

Maintained policies select reusable actions through typed invocation bindings. See [shared Controller actions, officer requests and fixed factions](controller-actions.md) for runtime inputs, channel remapping, outcomes, coordination and persistence. A shared widget can select `policy_template: "combat"`; it uses that same policy compiler and executor. Controller instances cannot override it.

## Power intent presentation

A numeric `intent_integer` channel without discrete `choices` can name a fitted `ui_system` as its presentation subject, for example `"ui_system": "drive.distribution"`. This lets an abstract allocation intent appear beside the physical group or cooling link it describes. Verification requires a fitted subject and a numeric intent. The subject is projected with the channel; it neither adds a physical consumer nor grants control or a new observation feed. The Power board pairs it with the permitted resource projection. Native power actions use their actual consumer bindings directly.

Power sliders submit values within the admitted setting channel's minimum/maximum range, including values between the old increment buttons. Slider travel follows the fitted generator rating, allocation budget, charge rate or cooling-link capacity. The client requires a currently projected human power channel; the server still checks operation/channel ownership, generation, action and range. These rules also apply to abstract power intents.

## Manual team destinations with assisted local work

A Team widget grants `team_assignment` to the human while its `team_work` or
`medical` operation remains delegated. Only destination orders (`auto/<compartment>`)
and cancellation use the assignment grant. Detailed clinical orders, patient
selection, other departments and readiness remain separately admitted. The same
operation mask is checked by native execution and remote presentation.

Repair teams retain their assigned destination while the Controller chooses useful local repairs. Medical chooses eligible treatment/recovery orders for that compartment after physically arriving. With no explicit destination, an occupied assisted team holds its current compartment rather than selecting another room. Vacant stations retain ordinary AI operation. Existing team destination, compartment-assignment, clinical compartment, travel and work fields persist this state; no timer or personnel progress is reconstructed on resume. Away manifest preparation remains independent and retains its ordinary department authority.
