# Autonomous ship operations

Ship Controller trees implement reusable operating policies. Scenarios retain the
objectives, job offers, away work, dialogue decisions and consequences. The ship
policies do not refer to mission, character, event or target IDs.

An away definition can opt in with `ai_dispatch: true`. Once the scenario reveals
that job, AI-owned Dispatch may open it, prepare its ordinary departmental
contributions, send the staged party when physical admission permits, and recall
it after its away tree reports completion. Jobs without this flag retain explicit
activation. Hidden jobs and recovered jobs are not automatically restarted.

For own-ship transport, Helm uses the offered target and its authored docking
ports. It ends ordinary navigation actions before submitting assisted docking,
retains the connection during deployed work by default, and requests normal separation after
recovery. A scenario away offer may instead request `ship_support: "combat_cover"`.
That reusable policy prepares an opening missile firing solution, separates after
deployment, manoeuvres toward the rear of an observed hostile and returns to dock
for recovery once the away tree reports completion. A contested approach also uses
that manoeuvre before committing to docking. Admission still checks operator ownership, current revisions, port
compatibility, observation, staging and physical transfer conditions. A scenario
offer cannot override human-owned operations.

`workflow_observations: true` exposes a bounded selection of the Console's
permitted manifest and docking rows, plus same-Console accepted navigation and
observed motion. It includes mission identity, phase and revision; docking port,
destination, phase and revision; launch and work-completion facts; target range;
travel distance, alignment and stopping distance. These facts neither dispatch
work nor read another Controller's private continuation. Remote away projections
retain the offer, work-completion and permitted-port facts.

Coordination is implicit through these blackboard observations and accepted
same-Console command sections, or explicit through the existing addressed officer
messages. There are no direct calls between AI policies. Assisted docking publishes
its planned approach point to the owning Console; impulse control reads that plan
without reading the docking Controller's private memory.

The shared own-ship `combat_status` observation reports missile availability and
physical missile-launch counts. Helm uses those receipts to end the opening turn;
it does not inspect Tactical's private tree. Combat-cover movement uses permitted
positional contacts and observed heading. Offensive target policies hold fire
while flanking and resume from the rear; point defence retains interception
priority. While ahead of a hostile, an outward orbit opens range rather than crossing
its firing approach. Once behind the contact, the ship closes within its fitted
offensive reach before resuming fire. A rear approach is a tactic, not a guarantee that every enemy has a blind
arc. If the contact is lost, the policy waits for permitted evidence.

Propulsion and steering read the same `workflow.cover_moving` fact. While moving,
both request the same destination; neither clears the other's guidance. At the
firing position both release destination guidance, propulsion stops and steering
faces the enemy. The opening response also applies when a threat arrives after
the party has deployed. Completed away work suppresses offensive fire during the
return and recovery, while point defence remains available.

Veil is physically suppressed while docking, launching or recovering a party and
for the existing interval after firing. A deployed party alone does not prevent
the detached ship from concealing itself. A ship-transport recall awaiting a dock
does not itself count as a physical transfer. Point defence can track and slew toward
an observed incoming missile before it reaches gun range; physical range, arc,
power and ammunition still determine whether it can actually fire.

Ballistic aiming refines the intercept time against observed relative motion.
Unguided rounds use that intercept time for elevation as well as horizontal aim,
so a fast descending missile is not aimed at two different future instants.

AI bridge control raises the fitted alert to its highest authored condition when
it observes a hostile target. The Courier reactor policy uses its observed fitted
maximum output at high alert. Target selection and actual weapon execution remain
separate; readiness does not manufacture ammunition, energy or a firing solution.
An AI policy that explicitly owns a weapon-target channel chooses a permitted
hostile independently of the scanner. Shared scanner following remains the
fallback when that channel has no AI target policy, and human target overrides
retain their ordinary authority.

Long-distance navigation can request impulse when aligned, clear of an observed
obstruction, beyond the cruise threshold and physically ready. It releases impulse
when navigation ends, alignment is lost, an obstruction appears or remaining
distance reaches the observed stopping distance plus an approach margin. Charging,
reserve, heat, cooling, actual acceleration and braking remain ordinary machinery.

Synthetic away integration covers opt-in offers, human ownership, physical docking,
team deployment, completion, recovery and saved continuation. Named rescue runs
are manual smoke evidence; they do not establish shipped balance or device
acceptance.

## Resuming deliveries from objectives

An original condition objective may include a `region` navigation destination and
an explicit `delivery` consignment. The scenario supplies the target selector,
port, source store, receiving store, material and quantity; optional
`material_overrides` select a material for particular ship definitions. Arrival
alone does not complete a condition objective. Its original receiving-stock
condition remains the authority for success.

The ordinary objective publication carries this information on the permitted
blackboard. Navigation selects the newest active objective with a destination,
or a valid explicit player priority. Completed, failed, cancelled, withdrawn and
stale objectives cannot strand an older active destination. A charted objective
location is sufficient to navigate, even without a radar contact. An active away
operation can interrupt travel without replacing or republishing the delivery.

Generic Helm policies hand over from navigation to docking when the objective's
target is observed with a compatible port. Both propulsion and steering observe
the same permitted handover facts; only the docking operation owner may request
the connection. They hold the connection while the delivery remains active and
use normal separation after completion.

The logistics owner submits the objective consignment through normal admission.
Saved private tree memory retains the accepted objective ID through interruptions,
preventing duplicate waiting requests. A refused submission retries; actual finite
stock, internal handling, port crossing and receiving work still determine delivery.
Human-owned operations retain their usual authority. No new return objective,
scripted post-rescue order or mission identity is needed in a ship policy.

The [rescue and delivery smoke](smoke/autonomous-delivery-2026-10-31.md) records
all eight original objectives completing, including the physical station delivery.

Active scan control reads the shared own-ship `workflow.veiling` fact and disables
its emitter while the funded cloak is operating. The passive radar and shared
target intent remain available. PDC readiness instead reads Tactical radar's
eligible missile threats: homing missiles need a current seeker lock or a radar-predicted collision
course, not merely a saved target ID. PDC can restart while Veil is requested if a missile
reacquires. Non-homing intercept threats retain the normal physical checks.

Missile seekers apply target concealment to their own acquisition range. Without
acquisition they retain their last heading and altitude solution, rather than
following hidden motion. Close reacquisition remains possible. Tactical's
`missile_locks` observation reports own-ship lock assessments only for missiles
its radar actually sees.

An interdiction boarder's decision to abandon or resume docking uses current
permitted position observations, or bearings it can engage with available
homing ammunition. The nearby true position of a hidden
ship cannot reset takeover progress or force the boarder to undock. Scenario
trees still own the target, arrival, dialogue and takeover consequences.
