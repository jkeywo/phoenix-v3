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

An explicit AI policy that binds a shared weapon-target channel owns its selection while that channel is AI-operated. Scanner following remains the fallback when there is no such policy; human overrides retain their normal authority. This permits a scanner to investigate a friendly casualty while an automated weapon independently selects an observed hostile.

`priority_or_latest` selects a valid explicit priority or the newest active objective
with a disclosed destination. Terminal, withdrawn and stale objectives are skipped,
so an interrupted unfinished objective resumes without a replacement instruction.
A condition objective may supply a charted region while retaining its actual
completion condition; navigating to that location does not require a radar contact.

`workflow_observations: true` exposes permitted job offers, manifest readiness, docking state and planned approach, and same-Console navigation/motion facts. See [autonomous operations](autonomous-operations.md) for the generic policies and scenario `ai_dispatch` offer. Controllers coordinate through these blackboards or addressed messages, without access to another AI's private state.

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

Evidence report templates set `evidence_report: true`, name a readable schema and omit payload `fields`. Their `send.<template>` action takes one integer: the `.id` metadata of a permitted record binding. Human `message.<template>` admission uses the same record identity. Admission checks current permitted evidence and copies its payload, original source, observation time, revision, uncertainty and withdrawal state unchanged. The forwarded message cannot extend the source receipt's expiry. Validation reserves no message identity; only successful execution emits traffic.

Report presentation parameters bind to that same evidence, for example `{"requested":{"evidence":"requested"},"tick":{"evidence":"record.observed_tick"}}`. Supported metadata is `record.subject`, `record.source`, `record.observed_tick`, `record.revision`, `record.uncertainty`, `record.kind` and `record.withdrawn`. Dynamic action arguments, memory and unrelated observations cannot supply report prose values. Record bindings also expose `.id`, `.age` and `.expires_tick` (`0` means no explicit deadline). Age and schema staleness remain distinct; authored policies can require fresher evidence for a particular action.

Two protected factual schemas supply reusable evidence. `phoenix.contact.report` contains measured position/velocity, heading, altitude, acquired classification/disposition and identity observation time; only declared sensor feeds receive it. It conveys a retained location, without granting current fire-control detection. `phoenix.team.inspection` contains the local compartment's structure and a complete repair-fault or Medical-patient snapshot, including an empty snapshot when no needs remain. Repair and Medical use independent streams. A team must be enabled, available, present and free of away reservation to observe locally. The team's owning Console and declared feeds receive its snapshot; the authored repair Console receives repair snapshots. No report producer chooses work or inspects another compartment.

Entity-wide messages use destination kind `ship` in the current serialized vocabulary. Shared Viewscreen publication requires an AI-origin message with explicit `emergency: true`; urgency alone never publishes. Publication requires a publishable schema and fields; dynamic public presentation parameters must bind a public payload value. Public copies retain identity and evidence but give spectators no action or acknowledgement authority. Ordinary targeted messages stay private. AI recipients retain relevant human, AI and factual-reporter deliveries; human notifications and player history display AI-origin messages only.

Console message destinations can use `id: "self"` to address the sender's Console. A named Console destination may declare an explicit `fallback`, such as `{"kind":"console","id":"engineering","fallback":"support"}`. The primary wins when fitted; otherwise the fallback must resolve. Authoring verifies the actual recipients' read permissions. No department-name heuristic or cross-ship fallback is used.

Officer reports use normal state-tree `send.<template>` actions, referenced reporting subtrees and their saved continuation. `report_observations: true` exposes summaries of permitted Console observations as `report.*` facts, including owned machinery constraints, grouped threats, team/craft status and scoped scan findings. Summaries do not choose work, create facts, send messages or acknowledge inboxes. Trees own wording, stable-condition delays, recovery and repeat suppression. Inline widget policies use the same path. See [officer reporting and history](messages.md).

Automatic reporting templates use `player_command: false`, so they never become human Send/accept/refuse controls. `operator_report: true` attributes the report to the effective owner of the observed work: `source_operations` can name its native operations (for example `medical` or `craft`); machinery reports use the Controller's physical command responsibility. The shared reporting tree still runs under human ownership and sends human-origin evidence to AI, while the human notification/history path admits AI-origin reports only. Ordinary AI decision messages retain AI origin. These flags do not authorise machinery writes or let clients choose an origin.

## Compatibility and verification

Workshop's **Console Controllers and complexity** inspector edits shared-widget
instances, equipment role bindings, star-to-level mappings and per-item data
placement. The shared-widget library edits commands, policies, responsibility
levels and named visual parts. The layout editor places parts and the rendered
ship-context preview checks each rating. Export/reopen retains authored bindings;
compiled per-instance policies are regenerated.

Controller trees expose command, System observation, peer-command and message bindings. Their task palette comes from the production registry for the resolved tree, including inherited bindings. Inherited binding groups remain read-only until explicitly overridden; removing a local group restores inheritance. System Properties expose factual reporters, and Coordination destinations include Console/Controller addresses.

Isolated Test feedback identifies each Controller by entity, Console and Controller ID, with its effective operator, generation, active states, transition reason, diagnostics and bounded command receipts. This debugging view is separate from player observations and does not export private tree memory.

World and portable session saves use schema 39 with per-channel and native-operation
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

World and portable saves before schema 39 require a new session. Existing save files are preserved; no continuation conversion is attempted.

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

## Scored objectives and Captain marks

See [the scored-objective contract](scored-objectives.md). Objective publications can declare `behavior`, `tier`, `behavior_target`, `scoring_policy` and `urgency`. Tiers use `reaction`, `scripted_high`, `objective_combat`, `directive_combat`, `scripted_low`, `objective_other` and `directive_other`. A region supplies a disclosed destination; `behavior_target` uses the existing scenario actor selector. Missing behavior defaults to delivery, navigation or observation according to the existing definition.

Controller trees can declare named `objective_scoring` policies. Each policy supplies nonnegative integer weights (0-1,000,000) for `urgency`, `progress`, `risk`, `travel`, `resources` and `commitment`. The built-in `default` weights are 1 for each evidence factor and 100 for commitment; these are defaults, not frozen balance. Travel estimates use kilometres from disclosed coordinates. The legacy `priority_or_latest` destination binding now uses scored selection rather than publication order. Unknown named scoring policies are explicitly infeasible for that Controller.

Captain's `objective.mark`, `objective.unmark` and `objective.clear` operations preserve the existing Controller generation and objective-board revision checks. Clear requires objective ID zero. Mark/unmark require an active published ID. Mark is idempotent. The legacy `objective.priority` operation remains an idempotent mark (zero clears all); its scalar tree observation returns the first marked ID only as a compatibility view. Decisions use the full set. Objective-board `selected` is an array of marked IDs.

World and portable sessions require schema 39. Older files remain preserved and require a new session. See the delivery record for the migrations still pending; adding objective metadata does not establish that every native decision loop has been removed.

Addressed evidence retains its recipient actor and expiry. A Controller reads Console-wide records plus receipts addressed to itself. Another Controller on the same Console does not receive those private records or message history. Human Console history remains available for inspection. Provider evidence uses the same recipient restriction as authored policies. Controllable ships continue to require explicit Controller or Console message destinations; legacy System inboxes do not become Controller broadcasts.

Console publications and feeds do not promote private receipts implicitly. A recipient may explicitly forward permitted evidence through ordinary message admission. Private sensor payloads do not become current Console-wide targeting facts. Record stream identity and bounded historical retention include the recipient, and expired receipts are stale even when their schema has no ordinary age limit. Save/restore preserves these boundaries; earlier continuations require a new session.

### Medical assignment observations

`medical_observations: true` exposes `medical.team`, `medical.destination`, `medical.revision`, `medical.available`, `medical.target`, `medical.urgency`, `medical.travel`, `medical.retask_cost`, `medical.continuing`, `medical.compartment`, `medical.reason` and `medical.condition`. The candidate producer consumes frozen team-local patient observations and the Console's own departmental roster reports. It cannot query the physical roster of another department. An idle automatically assigned team returns to its authored home; a human assignment prevents automatic destination replacement.

The root policy declares `team_commands: true` and a shared action binding. A short execution tree submits `team.order` with the reported revision and completes on acceptance. Changing destination/revision starts a new action identity. Acceptance starts travel; it does not claim treatment completion. Local authored clinical workflows continue treatment, stabilisation and recovery with finite supplies and facilities. Facility selection uses frozen availability and route reports; execution can refuse a now-full facility.

`away.care` uses the ordinary native Controller action signature: mission ID, `team/compartment`, mission revision. Only the assigned Medical team's owner can submit it, using the currently delegated Medical operation. Deployed teams report complete patient knowledge only in their current compartment. They travel only to an admitted destination and preserve that destination, route and local work continuation through checkpoints. A mission's hidden remote patients cannot cause an automatic destination change.

### Compiled objective behavior bindings

An `objective_bindings` array connects a published `behavior` to a declared shared `action`. Each binding declares `available` conditions, an integer `target` identity, typed action `arguments` and optional integer `factors` (`urgency`, `progress`, `risk`, `travel`, `resources`). Expressions use the ordinary permitted observations and literals. Defaults are the publication's urgency, 1000 expected progress and zero costs. The publication's named `scoring_policy` supplies weights; an unsupported reference makes that candidate infeasible.

During candidate evaluation only, `objective.record.*` exposes `id`, `target`, optional `destination_x`/`destination_y`, `urgency`, `age`, `behavior`, `tier`, `operation`, `compartment`, `system` and `mission`. Publications may scope work with these parameters; `operation` defaults to the behavior. Pass required values explicitly into the shared action's typed inputs. Missing coordinates remain unavailable. Applicability and execution availability are both checked before scoring. A write-only action with every channel delegated to a human cannot advance an objective. These helpers receive no World query port.

Policies using these bindings may report and coordinate through their ordinary trees, but select actions through the scored binding path. Direct `select.*`, `replace.*` or `write.*` tasks in such a policy, including referenced trees and attempt branches, are rejected. Selection uses ordinary action admission and cancellation. Stable objective/target/action arguments form the commitment identity; the bounded invocation identity and active execution continuation are persisted. Workshop and provider snapshots receive feasibility reasons, score terms and the selected candidate.

Ships explicitly fit `standing_directives` records with an `id`, supported `behavior`, `tier`, `scoring_policy`, `urgency`, `title_key` and optional `description_key`. They publish through the shipwide objective interface at registration, with ship provenance and a stable identity in the reserved range starting at 2^52. Scenario allocation stays below that range. Restoring a checkpoint restores existing records rather than republishing them. These records are marked `directive` and remain outside scenario completion/grading. A Controller without a matching binding reports `unsupported_behavior`; fitting a directive does not make an unsupported action feasible.

Objective bindings can author `retry_ticks` (2–1,000,000; compatibility default 20). An execution that ends in `action.fail` leaves a private receipt keyed by the shared action, target and exact typed inputs. Until its retry tick, all objectives requesting that same execution are temporarily infeasible. Other feasible work can proceed. Expiry allocates a new attempt identity through ordinary admission, without completing/failing the ship objective. New action inputs create a distinct request. Existing officer-request deduplication and lifecycles are unchanged.

Failure receipts and attempt counters survive checkpoints, with bounded retention and deterministic eviction by failure serial and identity. Operator-generation changes and ship capture clear them. Diagnostics expose `execution_backoff`, the next retry tick and attempt count only through the owning Controller's decision context. Cancellation and completed physical work retain their existing execution semantics.

Objective bindings default to `candidate_source: "single"`. A `medical` source requires declared Medical observations and evaluates the binding separately for each known team/destination and accepted continuation. Applicability, typed arguments and factor expressions see that candidate's immutable facts. Published `target`, `compartment` and `system` parameters constrain eligible Medical candidates. A request outside that known scope remains infeasible. Reports include route reachability, remaining travel, team availability, current escorts and facility availability. These are factual observations; the producer does not choose the next assignment.

`repair_observations: true` enables `candidate_source: "repair"`. It enumerates each delegated repair team's known destinations and accepted continuation from frozen structural observations, local fault snapshots, equipment symptoms and accepted repair requests. `repair_candidate.*` exposes team, destination, compartment, revision, target, structural deficit, request priority, travel, retasking cost, eligibility/reason and flags for continuation, home, disabled structure, request, symptom and fault. Authored bindings decide urgency and utility; the producer does not rank damage kinds. Published `target` scopes the owning ship, `system` scopes the team and `compartment` scopes the destination. A blocked route, unavailable team or human assignment makes that candidate infeasible. Home is available when that team has no known actionable assignment, using its own authored home.

Assignments use ordinary `team.order` with an explicit `auto/compartment` destination and revision. Continuing an accepted assignment need not reissue it. Cancellation preserves physical travel/work unless the replacement operation explicitly changes it. The local repair workflow chooses useful work only from its current compartment's complete snapshot. Legacy `repair.compartment` and `repair.system` facts describe accepted command settings rather than recommending departmental destinations. A non-assignment Controller may derive a repair-request parameter from one unambiguous reported location of its own equipment; distinct locations require authored candidates.

Medical policies author severity utility in bindings keyed by `medical.condition` (`severe`, `stabilised`, `minor`, `home`), along with travel and retasking costs. A `commitment` Boolean expression may recognise ongoing accepted work; its bonus still applies only within the same tier and selection pass. New assignments use the admitted `team.order` action. A separate continuation action leaves physical work running without repeatedly replacing the team order. Higher-ranked work can replace the Controller action and submit a different assignment through the same admission path. Existing treatment progress survives that replacement.

Point-defence target choice belongs to Controller policy. `tactical.intercept_target` identifies a permitted observed missile track selected by estimated closest approach, time to approach and stable track identity. The helper receives frozen position, velocity and altitude observations; it has no projectile target or source-faction lookup. In interception mode, `tactical.target` also supplies that track for legacy authored target policies. Shared Reaction bindings submit it through normal weapon target admission. A lower-priority standby binding can clear obsolete targets.

Weapon and directed target channels admit observed missile-track identities as well as ship identities, within the existing safe integer representation. Player target controls and accessible contact selection expose the same observed tracks. Interception mode permits physical tracking/aiming/firing at the accepted missile target; it does not select a different projectile. Current targeting evidence, supported interception class, range, ammunition, power and other physical constraints still gate execution. Manual mode/target changes remain independently admitted. An automated target choice requires a delegated target channel.

### Scored craft dispatch

`craft_dispatch_observations: true` enables `candidate_source: "craft"`. Each candidate describes one craft in a bay owned by the Controller's delegated `craft` operation. `bay.*` exposes identity, role, reported phase/order/target, pending admitted order, revision, report age, energy, ammunition, armament readiness, reserve condition, availability and current observed threats. Reports retain their last delivered values when a link is lost. Policy authors bound report age for new sorties and supply utility factors through ordinary objective bindings. An already accepted sortie continues in the background while the bay Controller advances other feasible work.

`request_observations: true` enables `candidate_source: "requests"`. It enumerates addressed, unexpired Manifest and Repair requests from the recipient's private continuation. Each candidate exposes `request.id`, `operation`, `status`, `mission`, `target`, `amount`, `priority`, `expires`, `revision`, `request_revision`, `evidence_revision`, `feasible`, `done`, `blocked` and `reason`. A published mission/target limits the matching candidates. These facts neither authorise a contribution nor expose an undisclosed mission. Accepted work awaiting evidence is infeasible for another acceptance action, leaving other objectives available. Repair acceptance requires an available delegated team and a known reachable compartment. Repair completion requires prior acceptance and a current local inspection showing restored structure and no remaining reported faults; admission checks that proof independently and carries its observation tick in the reply.

Dispatch uses `coordination.request_manifest(console, controller, mission, amount, priority, ttl)` or `maintain_manifest` with an additional retry delay. The mission must already occur in Dispatch's frozen observations. Replies use `coordination.respond_to(request_id, status, amount, reason)` to address the exact request. Acceptance may promise fewer assets than requested; it does not mean execution or completion. Completion requires a frozen, revisioned `department` receipt showing that the recipient's authorised contribution is prepared. Messages preserve mission, requested amount, request revision and evidence revision in both typed payload and presentation parameters. Ordinary contribution, staffing, transport and send admissions retain authority over physical operations.

A policy may declare an explicit same-Console `command_reads` binding named `bay.accepted_target`. Published engagement objectives may instead supply a scoped target directly. Bombing eligibility requires a currently observed hostile target; presentation labels do not supply target authority. `bay.attack_target` and `bay.attack_text` contain only that permitted target. A scoped `system` parameter selects a bay. Unsupported mission/compartment scopes stay infeasible. `bay.threat_present` combines observed hostile entities with incoming observed missile geometry; `bay.missile_threat` permits a separate Reaction binding for defensive sorties.

Prepared `craft.defend`, `craft.bomb` and `craft.recall` actions submit ordinary revisioned orders and distinguish admission from execution. Pending return may replace superseded undelivered guidance; waiting for delivery does not claim sortie completion. Launch, flight, servicing, ammunition use and recovery remain physical work. Initial accepted guidance captures the permitted contact sample instead of the target's actual hidden position. Flight decision acceleration, escort envelope and altitude guidance arrive through frozen own-ship observations. FTL mission transport reassignment and its remaining recall selectors are part of the separate away-preparation migration.

`away_preparation_observations: true` enables `candidate_source: "away"`. It enumerates known mission operations, declared departmental message routes, owned slot choices and explicitly identified transport/escort choices from frozen Console observations. `away_candidate.*` exposes mission/target/revision, operation/selection/system, peer identities, request status/retry age, eligibility, contribution/readiness, required-slot status and cost factors. Published mission, target and system parameters constrain those candidates. The helper cannot inspect World or another Controller's continuation.

Department acceptance and ordinary `away.contribute` admission precede asset assignment. Controllers choose each officer, team, transport and escort through shared prepared actions. Dispatch may coordinate preparation after an admitted human mission opening while the human retains mission/send authority. Authored bindings rank required slots above optional slots, retain pending requests without monopolising useful work, and retry refused execution. Automatic send applies only to an open mission; recovery after a human recall does not authorise another departure. Fitted standing away directives remain outside mission grading. Native code retains reservation, capacity checks, staging and finite transport/work execution.

Read-only candidate choices may be visible to a Controller while the corresponding operation belongs to AI. Player asset controls still require the human operation; showing a candidate does not grant authority. Craft choices use delivered reports and explicit craft identities. They do not select a legacy bay alias from an undisclosed live craft state. `repair.has_work` distinguishes reported repair work from the home destination in policies sharing the objective evaluator.

Departure preparation is a scoped readiness circumstance. The bay policy may recall craft through ordinary `craft.recall` admission; it does not gain authority over human-operated craft commands. `bay.recall_active` reports an already accepted party recovery so the policy does not repeatedly reset pickup work. Spent boarding torpedoes and irreversible flights do not become recall candidates.

Away observations expose `department.transport_released` and `department.recall_requested`. Recovery candidates enumerate explicitly identified, reported shuttle capacity and availability. Ordinary `away.transport` admission may reserve a replacement for a deployed party during FTL preparation; this does not authorise a new outbound sortie. Dispatch then admits the party's recall. Mission return, escort return and finite pickup/docking work retain their existing physical execution paths.
