# Scenario task planning

Status: Rescue under Threat MVP implemented and verified, 2 November 2026.
Evidence below is limited to the checks actually recorded; it does not establish
a completed human rescue playthrough.

## Contract

Tasks package participant objectives and physical opportunities/liabilities.
Different tasks can bind the same world circumstances. A pirate seizure task has
pirate objectives; it does not require a matching player combat objective.
Scenario objectives exist independently of their permitted publication.

The bounded HTN selects authored alternative methods and child dependencies.
Planning is hypothetical and never spawns entities or reserves stock. Selected
methods execute through independent existing scenario state trees. Controllers
continue choosing machinery actions from permitted scored objectives through
ordinary admission; task planning adds no privileged ship AI.

The world can invalidate a method or defeat an objective. Replanning retains
completed physical work, inventories, casualties, ownership, deployments and
receipts. It does not manufacture replacement assets or restore an intended plot.

## Foundation authoring

`scenario.task_plan` declares `root`, `tasks`, and optional `limits` (`depth`,
`nodes`, `expansions`; defaults 16, 256, 2048). Each task has an `id`, optional
`participants`, `objectives`, `opportunities`, `liabilities`, `complete_when` and
`fail_when`, and a nonempty `methods` list. Circumstances name declared typed
scenario facts; task objectives reference existing objective definitions.

Each method has an `id`, optional `requires`, `maintain` and `release_when` conditions, and either
a `situation` configuration or `children`. Child records have `id`, `task` and
optional `after` references to earlier siblings. Siblings without dependencies
can run concurrently. Recursive decomposition is rejected. Missing evidence
cannot satisfy a method. Alternatives are tried in authored order; bounded
exhaustion exposes no partially selected plan.

`seed` fixes portable method ordering for direct World/editor runs. With
`random_seed: true`, a new hosted session selects a seed once during setup.
The authoritative checkpoint carries it to peers and recovery. An admitted
plan cannot change its seed. GM inspection includes the actual seed.

Reusable pack `tasks` definitions support inheritance. Map `task_plan.imports`
instantiates a named `template` with an instance `id`, explicit `participants`
and `arguments`. Templates declare parameter names grouped into `facts`,
`situations`, `tasks` and `objectives`; the loader binds those references before
compilation. Missing, extra and mistyped arguments fail verification. The
authored import remains in the Workshop source; runtime definitions are resolved.

Task participants bind entities, ship slots, explicitly inherited participant
roles or typed entity facts. They are not another situation's private memory.
Situation factories receive the resolved roles once. Task children share an
explicit mission scope for objective/report queries, while retaining independent
instance identities, knowledge publication and completion credit. An objective
may declare `report_as` to contribute a mutually alternative definition to an
existing report row with the same actor and recipients.

`request_task(task, handle)` admits an authored follow-on task through an ordinary
scenario effect receipt. Repeating its token or named handle cannot duplicate
work. Requested tasks retain their source scope and stable request identity
across method changes. Tasks with `vary_methods: true` use a portable seeded
method ordering; other tasks retain authored preference order.

Trees in task-plan scenarios may declare typed `scenario_facts` observations.
These consume frozen authoritative scenario snapshots and are forbidden in
Controller contexts. They do not publish those facts into Console knowledge.

The current foundation starts selected situations, suspends superseded decision
trees without cancelling physical work, and retains their original continuation
and effect receipts. Failed physical conditions take precedence over simultaneous
success. Successful setup trees do not award objective completion: objective
completion remains the existing world-condition/geometry mechanism.
An explicit physical completion condition also governs compound tasks after
their children finish. Required direct-child failures propagate; an optional
child may fail without turning its parent into a failure. The rescue composition
root resolves the scenario lifecycle, while participant objectives and the
debrief retain their own success or failure.

Checkpoint task state is versioned within the scenario adapter and validated
transactionally with situation identities and tree continuations. Legacy maps
without `task_plan` retain their existing execution path.

Workshop's scenario inspector uses the shared structured controls and a shared
`gameplay-editors.scenarios.task_plan` seed; CLI seed/structured editing uses the
same backend. Reusable tasks use the same registered definition/record operations.
The GM Properties inspector shows task paths, selected methods, status,
selection/rejection or invalidation reasons, dependencies, situation identities
and release ticks. It reads the existing privileged inspection snapshot; these
diagnostics are not an additional Console knowledge feed.

Coarse attention demands name a participant, Console and amount (0–100). The
adapter supplies effective human operator identities; demands aggregate by the
actual person. `release_when` starts a persisted scheduling window, with earliest
and latest offsets measured from that boundary. A core task starts by its latest
boundary despite overload; optional work may be declined instead. Forecasts
reserve no physical stock. `duration_ticks` remains a presentation/forecast hint;
running commitments retain their demand until resolved rather than silently
expiring when an estimate elapses.

## Rescue content

`base.rescue-under-threat` has independent delivery, rescue, pirate and variation
branches. The arrival/distress anchor runs independently of delivery completion.
The existing away tree retains serial wounded/reactor/propulsion decisions,
optional Medical and its Security-enabled final seizure choice. Recovery waits
for that disposition to finish. Boarding, departmental preparation, recovery and
Morrow's departure publish ordinary actionable objectives to their participants.

Conditional scenario stocks select perishable goods from a usable cargo shuttle
with compatible capacity for the consignment. Both allowed player fits use the
same predicate. The selected cargo and assignment event retain the deadline even
if the shuttle is later lost. Mutually alternative delivery definitions contribute
to the same debrief row through `report_as`.

The authored offensive-weapon threshold selects the initial pirate budget. The
current fits yield three combat ships for the Cruiser and one for the Courier.
Screen and Boarder requires a viable screen. A one-ship force chooses between
Lone Raider and Concealed Opportunist using seeded feasible alternatives. The
concealed roster has a fitted Veil apparatus, power reserve, compatible specialist
and derived shared widget policy. Loss of screening capability changes the
selected tactic, while the encounter retains its committed roster and physical
continuation. Method changes never replace casualties or add equipment.

There are two optional packages:

- A relief shuttle with actual propulsion-compartment damage and 200 finite
  parts. Players can repair it to continue delivery to Cairn, or collect its
  stock through docking. Its logistics crew has no repair capability.
- An unarmed pirate tender carrying 120 finite ammunition units. Its ordinary
  Controllers approach and dock with a bound pirate participant for delivery.
  Losing the recipient or capturing the tender defeats its resupply objective.
  Target-authored boarding uses the existing Security/capture mechanisms.

The initial variation chooses neither package or one package. The main tree can
request the other for low repair stock or pirate resupply need. Typed admission
facts and stable task requests prevent repeat packages. Optional work has bounded
introduction windows and coarse attention estimates; core pirates have a latest
release boundary. Established physical work and published consequences continue.

Scenario spawn-group instance overrides compile into private fitted definitions
at load time. Spawned away templates instantiate stable operation/conversation
identities for matching physical targets. Overrides must preserve the compiled
compartment bindings and declared docking ports. Disclosure still travels through
ordinary Comms before an away operation becomes available.

World/portable session schema is 51; messaging kernel remains 6. Older incompatible
sessions require a new game, and their files are preserved.

Opportunity discovery, bounty generation and general GOAP search remain outside
this MVP. Unauthored recovery methods are not invented when a task has no feasible
alternative; diagnostics expose the blocked branch while the world continues.

## Evidence

The native `phoenix_scenario_tests` production-loader integration passes the
initial task cases: Missing evidence, preferred-method backtracking, bounded
exhaustion, independent state-tree execution, capability-loss replanning,
preservation of superseded situations, exact save/resume and transactional refusal
of an unknown saved method. Follow-on task receipts and shared-operator scheduling
windows also pass the native scenario integration. Shared seed insertion,
undo/redo and source ZIP round trips pass the native Workshop authoring integration.
Native cargo, away and interdiction integrations also pass the initial capability
selection, spawned physical repair and frozen-roster replanning cases. The native
mission/debrief and portable-session suites pass. These are synthetic checks,
not shipped-content balance assertions. Final native/WASM scenario and portable
session checks pass. Away, cargo and mission continuation JSON records match,
as does the interdiction continuation digest. The production pack and the exact
staged source content both pass independent verification. See the
[verification receipt](smoke/scenario-tasks.md)
for archive identity, retained evidence and partial authored journeys. Graphical
browser interaction and a complete human rescue playthrough remain separate
manual acceptance gates.

The prior [scored-objective rescue receipt](smoke/scored-objectives.md) proves
physical station delivery and departmental preparation. Its repair operation
remained open where authored goals lacked actionable bindings; it is not a
completed rescue claim.
