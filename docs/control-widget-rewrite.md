# Control widget rewrite

This record describes the console/Controller rewrite agreed on 28 October 2026.
Implementation is complete and verified on 29 October 2026. Shared widgets now provide the Console ownership, policy, presentation and Workshop authoring paths.

## Contract

A logical control widget owns one Controller and explicitly binds compatible
equipment. Reusable definitions provide commands, one shared state tree, ordered
responsibility levels, named visual parts and named data. Ship instances map
stars to levels. Per-item Main/Details/Hidden presentation is independent of
responsibility and can vary between stars sharing the same level.

An Automated level has no human-owned commands. An occupied station uses the
selected level's command allocation; absence, disconnect and Away give every
command to AI. Presentation visibility never selects authority. Higher levels
retain responsibilities or explicitly replace assisted commands with detailed
equivalents. Existing gameplay responsibilities and physical execution remain
the migration baseline.

The shared tree attempts commands under all workloads. Attempts return Applied,
AlreadySatisfied or Refused. Matching human-owned settings succeed without a
write. Requests keep identity/receipt semantics; equality is not a new execution.
Ordered attempt branches can fall back within one tick. Earlier successful
commands survive branch failure, but failed/budget-exhausted evaluation commits
neither staged commands nor continuation. Physical completion remains separate
from accepted intent.

## Delivery status

The maintained `content/base`, `content/test` and `content/test-3d` packs now use
shared-widget instances. Their original mechanism, resource and hull authoring is
preserved; Console bindings, presentation and responsibility declarations use the
new model. Ship variants inherit definitions and may bind independent weapon sets
or map the same widget to different levels and visual locations.

The runtime and Workshop use schema-2 Controller layouts exclusively. Implicit
ownership conversion, schema-1 human/AI allocations, `manual_workloads` and
`individual_stars` are retired. World and portable-session saves use schema 34;
older saves require a new session. The wire command envelope remains schema 5.

Final verification on 29 October 2026:

| Gate                                       | Result                                                                                 |
| ------------------------------------------ | -------------------------------------------------------------------------------------- |
| Native application build and build check   | Pass                                                                                   |
| Complete native integration suite          | 49/49 pass                                                                             |
| Browser client, Workshop and server builds | Pass                                                                                   |
| Affected WASM integrations                 | 18/18 pass                                                                             |
| Fresh native/WASM continuation comparison  | Controller, simulation, Medical and craft match                                        |
| Maintained pack verification               | Base, test, test-3d and base plus Test Missions pass                                   |
| Explicit native widget journey             | Readouts, Details, input, native workflows, compact reunion and Workshop previews pass |
| Real-ship native Console captures          | Courier and Cruiser Helm, granted pages and compact layouts pass                       |

The WASM gate includes knowledge, shared bindings, Workshop tests, local sessions,
input/semantics, Controller execution, flight, directed systems, Medical, craft,
Away missions, FTL, Personnel, logistics, conversations, cargo, content and simulation.
The Controller continuation digest for this run is
`515c891f6677c42e6752c908e86200232c2b615b6ef169fc65d27943bec1e47e`;
it is a receipt, not a golden test expectation. Traces are compared within each run.

Logs and captures are under `build/widget-*`. Physical screen readers,
six-display hardware, physical controllers and browser graphical journeys remain
separate device evidence; these software checks do not claim to exercise them.

## Current definition vocabulary

`control_widgets` is a pack definition table with ordinary dependency/override
and inheritance resolution. A definition declares `schema: 1`, `equipment`,
`commands`, optional `tree`, ordered `levels`, named `data` and `parts`.

Native workflow responsibilities are declared in `operations`, mapping each
operation name to its equipment roles (for example `team_assignment: ["$teams"]`).
The level's `human` set selects command channels and native operations together.
They share operator generations, absence/Away handover, checkpoints and remote
projection. A native operation is scoped to its declared equipment; owning one
operation does not grant another operation on that equipment.

The first level is `automated`, with no human commands. Subsequent levels declare
their complete `human` command set. `replaces` maps a retired assisted command to
its detailed replacements. Data defaults use `main`, `details` or `hidden`;
per-level defaults and per-instance `data_placement` overrides resolve in that
order. Instance overrides use star keys `1`, `2`, `3`. Required main indicators
cannot be moved to details or hidden.

A schema-2 `control_layout` Controller instance declares `widget`, `equipment`
role bindings and `star_levels` for all three ratings. Equipment placeholders
such as `$engines` in command consumers bind to an explicit identity or set.
Instances cannot replace shared commands or trees; derive a library definition
for intentional differences. Profiles declare their identity and `stars`; there
is no second authored command-allocation list.

Controller tree task `attempt` contains ordered branches, each with optional
`conditions` and ordered `tasks`. Optional `result` names boolean private memory
for the outcome of the entire block. Nested attempts and referenced trees retain
normal private continuation. Refused commands do not consume one-shot markers.

## Verification boundaries

The new synthetic Controller fixture goes through the production pack loader,
definition compiler, World, command admission, state-tree execution and physical
engine bindings. It covers partial ownership, preferred-branch refusal,
same-value satisfaction, retained earlier writes, continuation and independent
presentation placement. No production ship tuning is used as an expected value.

Native/WebAssembly builds, content verification, complete relevant system suites,
Workshop round trips and explicitly invoked graphical/device journeys remain
required at their respective implementation gates. A compiled interpreter or a
permitted data model does not establish a delivered player console.

The explicit graphical journey uses `python tests/manual/control_widgets_ui.py`,
then `phoenix_control_widgets_smoke build/control-widgets-ui.zip`. It exercises
readouts, modal Details, commands, continuous ranges, native workflow controls,
independent instances, compact reunion and Workshop previews at all three stars.
Production inspection uses the same executable's `--inspect` mode for real ships.
These are synthetic UI/semantic checks; physical screen readers, six-display
hardware and physical controllers remain separate device evidence.

## Follow-up code review

The follow-up review closed these gaps in the shared-widget path:

- Model snapshots, committed decisions and inference scheduling now allow the AI
  part of an interactive widget. Operator generations still invalidate stale
  model input. Command can select a stance for delegated widget commands while
  human commands retain their allocation.
- UI command admission and manual binding selection check the individual channel.
  Widgets only collect explicitly allocated channel controls; opaque legacy
  native controls cannot appear merely because their Controller identity matches.
- State-tree guards read staged accepted/revision/consumed/request/pending facts,
  alongside staged command values, during a single evaluation.
- Each wide and compact layout is checked at every star for human command
  access, main data and a Details entry point. Star conditions are considered;
  an optional runtime capability cannot establish guaranteed access without a
  fallback part.
- Shared command consumers must use declared equipment roles. Instances cannot
  override the shared policy, and widget levels cannot be mixed with legacy
  allocation profiles or `individual_stars` command gates.
- Workshop's ship/Console context now drives the rendered shared widgets and
  responsibility/data mappings at each star. Sample control edits remain in the
  isolated preview. The preview context uses its own renderer, preventing live
  Console geometry from appearing in its offscreen capture. Its graphical
  journey checks pointer input against the sample command state. Tree editing
  uses the interpreter's `once: false` default.

The native Controller suite covers these runtime and authoring refusals through
production loading and execution. The graphical journey additionally renders
Workshop previews at all three stars. The maintained Console migration and native operation adapters now use these same paths.

## Simplification review

The widget compiler now binds an already compiled definition. Both the ship
compiler and Workshop sample reuse that definition instead of repeating its
validation during binding and responsibility mapping.

Widget buttons, ordinary Console controls and command feedback share the
existing Console label formatting, including numeric values, enable/disable
wording, execution subjects and selected state. The presentation model no longer
stores unused copies of the level, star count or availability flag.

Workshop sample commands use `eligible_console_command`, the same advisory
admission used by ConsoleModel. The sample keeps complete command envelopes in
one map instead of maintaining parallel value and request maps. Repeated runtime
checks use `controller_ai_running` rather than spelling out the widget exception
independently.

Ordinary widget evaluation relies on the interpreter's existing continuation
transaction instead of copying continuation a second time. Interrupted tree resume keeps its copy so a failed evaluation also rolls back the resume. Ordinary ownership changes retain the policy continuation; selected actions and officer commitments are cancelled, and generations invalidate stale input. Held inputs are neutralised only when their human ownership changes.
Fallback guards and ordinary transitions share one comparison compiler, rather
than maintaining separate operator and type-checking implementations.

The layout sample remains an isolated presentation preview. Physical execution
continues through the existing Workshop gameplay test. State-tree command staging
also remains: same-tick fallback and rollback require it, and it is not a second
physical-system implementation.

## Native workflow migration

Native Team, Personnel, conversation, craft, logistics, docking and departure
adapters now accept widget operation grants rather than treating any human
channel as authority over the whole Controller. Personnel commands carry both
the operator generation and the independent roster revision. Operation names
are validated and overlapping owners for the same operation/equipment are
rejected. Scoped read models and Console admission carry these grants remotely.

Native state-tree commands preflight through the existing domain admission
functions with execution disabled. Refusal participates in the same ordered
attempt fallback as scalar commands. Accepted outputs execute only after tree
evaluation succeeds. One mutation of a native revision domain is staged per
evaluation, so a second command cannot silently reuse stale revision facts.
Coordination preflight uses an isolated protocol continuation and the existing
message validator; physical execution remains in its current domain adapters.

The synthetic Controller suite exercises a physical team's human destination
refusal, unattended assignment by the same tree, operation scope conflicts and
checkpoint continuation. It also covers unclaimed widget initialization with
explicit AI command masks. The complete native Controller and Medical suites
pass. Maintained ship layouts and runtime ownership now use this contract exclusively.

## Shared policy and encounter continuation

Star-to-level mappings can select an Automated level at a higher star rating.
The levels themselves still describe an explicit responsibility hierarchy;
star mappings can retire an assistance panel when another widget replaces it.
A synthetic runtime case verifies that changing to that rating releases its
commands to AI.

The existing officer-policy compiler now binds shared widget commands before
building its policy. Generated policies are recorded as compiled per-instance
bindings; authored instances do not acquire command/tree overrides. Carried
transport guidance derives a shared widget with its extra altitude channel,
keeping its equipment role explicit. The existing officer-support capability
remains available on a shared widget in the Controller integration case.

Encounter overrides now act on individual delegated channels. A mixed widget
continues evaluating unrelated duties and receives a refusal for an encounter's
reserved commands. Docking and departure overrides also check their operation
allocation. Native Controller, carried-craft and interdiction suites pass; the
maintained source packs pass production verification.

`phoenix_verify --inventory output.json base.zip [mod.zip ...]` exports resolved
entity definitions after ordinary verification, so migration and audits use the
production assembly/equipment resolution rather than a second implementation.

## Native visual parts and setting ranges

A part binding can select an existing native workflow renderer with `renderer`:
`teams`, `personnel`, `craft`, `operations` (logistics), `docking` or `away`.
It must declare the corresponding operation scope. These renderers read the
same permitted ConsoleModel boards and submit through its existing actions.
A part's operation list filters its roster and available controls; another
widget's equipment is excluded. Automated operations produce readouts, and
owning an unrelated scalar channel does not expose delegated workflow buttons.
Compact `main`/`complete` presentations reunite the named native parts.

`ranges` lists setting channels displayed by a part. It reuses the existing
Console range input, admission and pending-value reconciliation, including
routing to a granted Console. The same setting is not also emitted as a palette
of preset buttons. Main/Details/Hidden data remains separate from these control
allocations. Modal Details blocks both background buttons and range input.

Native browsing state is keyed by ship, Console and logical Controller, so split
parts share state and separate widget instances remain independent. The
graphical journey checks both instances through a compact-layout switch.

Native panel readouts update in place through one shared structure builder,
retaining focus and scroll position. Workshop exposes rendered control bounds
for pointer inspection; the graphical journey uses those actual bounds rather
than a fixed screen coordinate.

The extended synthetic native journey passes range changes against an actual
Controller setting, modal blocking, operation-specific native controls, compact
reunion and Workshop pointer updates (`build/widget-finish-native-widget-ui.log`).
The Controller suite passes, and the module boundary checker reports zero
violations. The isolated build directory is `build/widget-finish-native`.

## Away responsibilities and intrinsic Bridge widgets

Away preparation now admits each automatic action as AI against its operation
allocation. Team destination, useful work, high-level Team selection, manifest
transport, officer mission staffing, contribution authorisation, dispatch and
supply quantity are separate responsibilities. Human Team selection reuses the
existing assignment path through explicit select/unselect requests; it does not
grant control of automatic manifest preparation. Revoking a contribution clears
its departmental assignments as part of the authorised operation.

Alert, stance and objective priority use intrinsic operation-only widgets. Their
commands carry both the widget operator generation and the existing native
revision. World admission, remote advisory descriptors and state-tree attempts
use the same domain adapter. These widgets require no invented physical officer
post; fitted and department Controllers retain their duty requirement.

Named data may bind a scalar JSON pointer within the already permitted gameplay
board using `board:/path/to/value`. It provides no additional world visibility.

The native Controller, Medical and Away integration suites pass, including mixed
Away authority and Bridge remote admission/continuation. The maintained source packs have shared widget placements for their Consoles and pass the production verifier.

## Individual equipment members

A shared widget can declare `repeat` naming one multiple equipment role and an
inline `member_policy` instead of `tree`. Author commands, data and levels once
against that role. Authoring expands each attached member into separate command
channels and an ordinary referenced state tree under the same Controller. Native
execution, staged admission, fallback and per-call continuation remain unchanged.
Member policies bind scalar System observations through their equipment roles;
actions and cross-controller command reads belong in ordinary explicit policies.

Level responsibilities, data destinations, named-part commands/ranges/value
filters and instance per-star data overrides expand with the member channels.
The original common-command fan-out remains available by omitting `repeat`.
Workshop uses the same expansion for its ship-context presentation preview.

The synthetic Controller case checks two members with different physical
observations, human-setting refusal/fallback and unattended individual decisions,
then restores their independent continuations. It passes in the native Controller
suite. Optional granted-console widget pages use the existing permitted additional
Console projections; their visibility does not weaken required coverage in the
actual owner's layout. An omitted-widget regression protects that distinction.

## Role-bound policies and production Console integration

Shared widgets may declare an inline `policy`, including ordinary tree inheritance,
whose `$role` bindings resolve against the instance equipment. Generated definitions
and programs are compilation artifacts: authored widget ids, equipment bindings and
data overrides remain intact on export and recompile. Repeated member policies use
the same elaboration and the ordinary evaluator; they have independent continuations.
Workshop edits and previews these declarations through the production compiler.

All three maintained base packs use the shared-widget vocabulary and pass verification.
Native Controller and carried-craft integration checks pass, including re-preparing an
edited inherited policy. The graphical journey verifies readouts, details, ranges,
operation admission, independent instances and compact reunion. Real production
Console captures exercise Courier and Cruiser Helm and Support. Bounded scroll groups
keep flight widgets beside the radar, and granted Consoles use their own projections
and independently selected star ratings. Native objective rendering is shared with the
Bridge widget; FTL and coordinated impulse retain their existing command protocols.

## Final ownership details

Native workflow grants cover Team assignment/work, Medical, Personnel, department
contribution, mission staffing/lifecycle/send/quantity, craft, transport, logistics,
docking, departure and Bridge operations. Their existing revisions and physical
execution remain authoritative. Default stock assistance defers to an explicitly
authored stock policy. Automatic manifest preparation fills empty assignments and
retains the team's explicit selection.

Coordinated impulse is an explicit native operation on each participating widget.
A low-star level can expose the group launch while retaining AI-owned individual
triggers. Requests retain existing generation, receipt and replay rules, including
an initially idle request channel.

The same policy runs while any or all channels are human-owned. Earlier accepted
writes are visible to later tasks in the same decision; physical observations stay
fixed. Authors must place dependent work in the successful attempt branch. A
receiver that cannot apply a requested setting must retry or fall back before
acknowledging it. Entry actions do not replay merely because the operator changes.

Environmental actors without equipment or player Consoles keep no invented widget
layout. Actor creation, ticking and restoration support that empty equipment path;
a synthetic lifecycle case protects it alongside the real-ship graphical journey.
