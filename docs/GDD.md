# Phoenix V3 — Game Design Document

**Design baseline · 17 October 2026; Controller architecture revised 19 October 2026**

Phoenix is a spaceship bridge game about solving changing problems together through complementary expertise. A crew operates a working vessel in the Settled Dark: interpreting incomplete evidence, negotiating demands, committing finite resources and coordinating action. The ship responds as one connected machine, while its officers retain distinct information, responsibilities and decisions.

This document defines the intended V3 game. It does not claim that any V3 capability is implemented or validated. It consolidates the reviewed design into one specification; the historical references are evidence of its decisions, not additional requirements to discover elsewhere.

## Contents

- [1. Experience and boundaries](#1-experience-and-boundaries)
- [2. The Settled Dark](#2-the-settled-dark)
- [3. Crew, stations and participation](#3-crew-stations-and-participation)
- [4. Authority, knowledge and assistance](#4-authority-knowledge-and-assistance)
- [5. Flight, passage and operating geometry](#5-flight-passage-and-operating-geometry)
- [6. Sensing, investigation and evidence](#6-sensing-investigation-and-evidence)
- [7. Communications and psychic warfare](#7-communications-and-psychic-warfare)
- [8. Combat and protection](#8-combat-and-protection)
- [9. Energy, heat and material supply](#9-energy-heat-and-material-supply)
- [10. Damage, restoration and loss](#10-damage-restoration-and-loss)
- [11. Personnel, craft and away missions](#11-personnel-craft-and-away-missions)
- [12. A world that continues working](#12-a-world-that-continues-working)
- [13. Scenarios, modes and continuity](#13-scenarios-modes-and-continuity)
- [14. Fitting, authoring and presentation](#14-fitting-authoring-and-presentation)
- [15. Validation scenarios and acceptance](#15-validation-scenarios-and-acceptance)
- [16. Authority, traceability and remaining specification work](#16-authority-traceability-and-remaining-specification-work)
- [Appendix A. Catalogue coverage](#appendix-a-catalogue-coverage)
- [Appendix B. Architecture decision reconciliation](#appendix-b-architecture-decision-reconciliation)

The agreed [Console Controller architecture](controller-architecture.md) refines the runtime authority and information contracts below. Its [refactoring plan and delivery record](controller-refactor-plan.md) separately track implementation and verification; this GDD remains the design contract.

## 1. Experience and boundaries

### 1.1 The experience promise

Be the crew of a working starship under pressure. Each player should have a worthwhile operating problem, meaningful interventions and reasons to listen to colleagues. Coordination is central, but each specialist decides how to operate their own machinery. A successful crew understands how its choices affected the outcome.

The core loop is to observe, interpret, discuss priorities, prepare, act and reassess. Combat, rescue, investigation, negotiation, logistics and recovery all use this loop. Situations should permit more than one supported response. Scenarios supply circumstances and goals; physical rules, actor behavior and crew decisions determine what happens.

Ordinary missions target **45–90 minutes**. Short practice exercises and longer continuing operations remain valid. Routine travel is compressed. Save/resume lets a group stop without treating unfinished work as completed or resetting its consequences.

### 1.2 Design pillars

| Pillar                                   | Design consequence                                                                                                                                                     |
| ---------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Complementary problem-solving            | Specialist decisions change other officers' options. Avoid isolated minigames and one player directing every consequential choice.                                     |
| Communication grounded in work           | Distribute evidence and authority. Players report interpretations and negotiate feasible actions rather than copy codes or acknowledge routine prompts.                |
| A connected, understandable situation    | Resources, machinery, actors and obligations have causes. Show enough evidence to explain consequences without giving every station world knowledge.                   |
| Personal depth with competent assistance | Authored Console complexity profiles offer coherent workloads. Delegation removes execution burden without making AI deliberately incompetent or changing the physics. |
| Prepared margins and recovery            | Reserves, readiness and useful automation buy time to respond. Failures create changed options before they create exclusion.                                           |
| Accessible ordinary play                 | Browser controls and installation-free phone participation provide a complete route. Native clients, elaborate bridges and venue effects are optional extensions.      |

Activity is not measured by clicks. Brief quiet periods, listening and preparation are legitimate. Repeated maintenance, mandatory approvals and arbitrary emergencies must not be used to fill every moment. An assisted player remains entitled to meaningful choices and influence; assistance does not oblige the Director to generate replacement chores.

### 1.3 Product boundary

Support autonomous scenarios and optional GM-led play, single-ship and multi-bridge situations, cooperative work and competitive team scenarios. The crew-size design target remains two to eight humans through suitable authored ships, with six as the reference crew and ordinary AI covering vacant seats. The new starter content specifies two- and six-seat ships; larger rosters require later authoring rather than importing old hulls. Shared challenge is selected explicitly; reducing the number of humans principally changes AI coverage, rather than automatically weakening opposition. Personal station depth is separate from challenge.

The complete design includes continuing Patrol, War and Sandbox modes. Inclusion here is not a promise that all features ship in the first playable build. A delivery plan follows this GDD separately. Ordinary scenarios must remain playable without an account, native installation, GM, custom peripheral or manual local-network configuration.

External operations are managed through consoles and simulated teams. There is no directly playable first-person/on-foot mode in this baseline, and no initial requirement for players to pilot small craft themselves. Mining supports bridge work; it does not become a compulsory resource grind. There is no general market-price economy, separate colony-management game, account-level equipment-unlock requirement or extra combat-load/flux meter.

## 2. The Settled Dark

### 2.1 Historical frame and player position

The Settled Dark comprises roughly two hundred inhabited worlds, stations and void-habitats. Dynastic Houses, Guild navigation and an old institutional faith hold a materially constrained civilisation together. Ships cross interstellar distances through the immaterial deep with psychic navigators.

Phoenix takes place **immediately before the Ascendant's conquest**, while Conclave authority is uneven and exhausted. The conquest is outside the opening frame; only an explicitly authored campaign or GM introduces it. Later Void Throne politics and particular scenario inventions are not Phoenix's present.

Starter crews serve **House Caelifen** as professional household personnel under broad standing orders. Their initial operating area is an original district on the settled frontier: a principal Caelifen hub, smaller ports, mixed surrounding jurisdictions and routes toward lost human connections and unknown space. Other campaigns can choose different patrons, obligations and relationships.

Decline appears as concrete pressures—missing supplies, disputed authority, deteriorating facilities and dangerous routes. It is not an automatic decay system that erases successful interventions. Crews can save lives, restore services and earn breathing room. Worthwhile work, selected hard compromises and occasional overt horror establish the tone; depictions of suffering are restrained and religion is chiefly institutional background.

### 2.2 Technology and social meaning

Ordinary engineering is understood but constrained by materials, facilities, expertise and supply. Veteran industrial ships remain maintained and capable. House designs and doctrines differ within shared engineering principles. Heraldry, equipment provenance and working machinery give ships their identity; consoles use retro vector holographic styling.

| Capability                   | Settled Dark expression                                                                                                                                                                                 |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Interstellar passage         | Guild navigator, fitted apparatus, route knowledge, machinery and finite transit fuel.                                                                                                                  |
| Local jumps and displacement | Specialist passage apparatus, sharing the active navigator capacity with interstellar passage.                                                                                                          |
| Shields                      | Engineered fields with directional protection and measurable response characteristics.                                                                                                                  |
| Propulsion                   | Reaction engines; routine manoeuvring propellant is abstracted, while operating energy and heat remain meaningful.                                                                                      |
| Weapons                      | Kinetics and missiles predominate; beams are specialist equipment. Destructive deep-based weapons are rare.                                                                                             |
| Psychic operations           | The entire former EWar family becomes psychic attack and defence, with actual effects on signals and equipment. House-trained specialists need fitted apparatus and are distinct from Guild navigators. |
| Delegation                   | Watch personnel and bounded automation. Genuine machine minds are exceptional and difficult to reproduce.                                                                                               |
| Medicine                     | Familiar restorative prosthetics and selected enhancements; treatment of minor injuries and stabilisation of severe casualties.                                                                         |
| Fabrication                  | Consumables and common parts can be made aboard appropriately equipped ships; specialised components require supply or salvage.                                                                         |
| Long-range messages          | Interstellar messages travel aboard ships; local communication uses supported equipment and links.                                                                                                      |

Extraordinary capabilities have stated operating limits and counterplay. Grittiness does not require arbitrary lethal mishaps. Investigations can answer local practical questions while larger cosmological mysteries remain unresolved.

### 2.3 Starter content boundary

Define a new **two-player Courier** with a complement in the dozens and a new **six-player Cruiser** with a complement in the thousands. They are operational concepts, not imported V2 assets. The Cruiser includes a local-jump fitting. Fittings, compartments, names, exact statistics and visual assets are authored later.

The Courier separates movement/weapons decisions from sensing, communication and ship support, using ordinary assistance. It must not require two people to manually operate six full stations. Science-owned passage belongs within the small ship's corresponding mission/support bundle rather than requiring a third player.

Three introductory situations follow a frontier route under strain: routine work, assistance and a contested operation involving rival-backed privateers, with one bounded excursion beyond reliable charts. They are linked but independently playable. This establishes teaching and mode needs, not named locations or fixed plots. Old Destroyer, Battleship and faction catalogues are not V3 content commitments. Larger authored crews remain an extensibility requirement, with no inherited hull roster assumed.

## 3. Crew, stations and participation

### 3.1 The reference Cruiser

| Station     | Owns consequential decisions about                                                                                                                            | Depends on colleagues for                                                                        |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| Captain     | Alert condition, supported stances for fully AI-operated Consoles, and the Objectives list with a priority objective when any ship Controller is AI-operated. | Crew conversation and each specialist's assessment of feasible actions.                          |
| Helm        | Motion, heading, exposure, approach, docking and supported flight assistance.                                                                                 | Target/route evidence, passage coordination, power and operational geometry requirements.        |
| Tactical    | Weapon targets, engagement/interception priorities, Security, combat craft and general Dispatch.                                                              | Findings, illumination where useful, geometry, resources and away-operation needs.               |
| Engineering | Generation/distribution, coolant, repair/containment, main stores, reserves, utility shuttles and supply Dispatch.                                            | Predicted demands, delivered fault evidence, safe access, transport and negotiated supply terms. |
| Science     | Investigation, 360-degree focused scanning, shields, Medical, displacement, Navigator and Phantom choir/Psychic ward.                                         | Positioning, power/cooling and other stations’ Dispatch requests.                                |
| Comms       | Dialogue, received claims, requests, negotiations, opportunity discovery and consequential away-party decisions.                                              | Known terms, viable links and other stations’ physical execution.                                |

Assignments are authored through ship/station/console data. A role name must not be a hard-coded permission or physics rule. One authored roster per hull is sufficient for the initial content; arbitrary live rearrangement of station bundles is not a player setup task.

Captain can coordinate a request for maximum thrust, but Engineering decides feasible power allocation and Helm decides motion. Science can provide an illumination target without selecting Tactical's weapon target. Comms can negotiate a rescue without creating transport capacity. These distinctions must remain visible in controls and feedback.

### 3.2 Complexity and delegation

Players choose an authored complexity profile for each Console in the lobby or that Console's settings menu. The profile determines which Console-owned Controllers are human-operated and which use ordinary AI; there are no individual delegation toggles or per-System complexity settings. Station-depth descriptions below describe the resulting workload. Each Controller owns its own command section, and its effective human or AI operator writes through the same boundary. Simplified controls convey supported intent without granting a second writer to an AI-owned section. Scenario-required human work can use explicit human-seeking controls.

The [Cruiser Helm, Tactical and Engineering workload contract](cruiser-complexity.md) settles their one-, two- and three-star responsibilities. Tactical owns general Dispatch; Engineering owns utility shuttles and supply Dispatch. Department contributions preserve local asset authority, and negotiated supply offers use physical cargo collection and unloading. Other Cruiser stations retain their existing complexity designs.

| Station     | Simple human activity                                                                                                                                                            | Additional depth                                                                                                                                                                                                                     |
| ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Captain     | One star: select alerts, eligible AI Console stances and a priority from the ship's Objectives list.                                                                             | Single profile; no additional star levels, machinery reserve, Command Duty Officer roster or reporting/display dashboard.                                                                                                            |
| Helm        | Desired speed and heading, with optional assistance and direct-control toggle.                                                                                                   | Depth comes from momentum, geometry and coordination, rather than artificial mandatory tiers.                                                                                                                                        |
| Tactical    | Basic weapon targeting; delegate flight operations and security.                                                                                                                 | Middle adds weapon-group targets and offensive/interception priorities. Detailed adds optional particular-craft assignment and whole-team security objectives. Goal-level dispatch remains available.                                |
| Engineering | Prioritise coarse power groups and select no/low/high coolant use; routine repair and stock decisions are delegated without policy setup.                                        | Middle adds individual coarse power/coolant allocations and System restoration priorities. Detailed exposes fine groups, optional component/team dispatch and main-store/resupply decisions. Fine groups subdivide the coarse model. |
| Science     | One star: shared scanner target, shield raise/lower and facing priority, individual Medical compartment assignment, mission contributions and FTL controls; automatic self-ward. | Two adds scanner targets, clinical orders/patient priorities and psychic choices. Three adds full shield/fitted equipment settings, medical stocks and officers. No Science Dispatch or shuttles.                                    |
| Comms       | One star: hails, conversations, negotiations, requests, opportunities and away dialogue; automatic radio/reserve/officers.                                                       | Single profile. Psychic controls move to Science; Veil moves to Courier Support.                                                                                                                                                     |

Captain's single-profile design was agreed on 23 October 2026. Its Objectives list is implemented as a dedicated GUI element. It displays the ship's published objectives from the entity blackboard. Whenever any Controller is effectively AI-operated, including delegated Controllers on human-operated Consoles, Captain can select one active priority objective or clear it. The accepted selection is stored with that objective list; ordinary Controller AI reads it and takes it into account where relevant, feasible and consistent with its existing orders and commitments. It is advisory and cannot override human control or reveal private scenario state. See the [Captain design agreement](bridge-command.md#captain-design-agreement--23-october-2026) for lifecycle and delivery boundaries.

The other station-depth candidates require workload validation, especially Science's passage responsibility. Exposing a control does not require continuous manual execution. Deeper presets must not impose clinical procedures, manual arithmetic or per-shot authorisation. An accessible simple station must still contribute meaningfully in combat and non-combat situations.

### 3.3 Join, handover and loss of participation

Ships author an ordered alert-condition list, normally five levels, with labels, colours, optional screen-edge glow, an entry siren and condition-specific music. Accepted alert state belongs to the entity blackboard and is readable by all Consoles and Controller trees. Alert controls are placed independently of Command: Courier Support owns them, while the reference Cruiser places them with the Captain.

Consoles author named stances that guide their AI Controllers. Command can select stances for fully AI-operated Consoles; a Console's own human can select the stance used by its delegated Controllers. Command is normally human-seeking and may automatically hide when no eligible targets remain. Human-seeking Consoles may supply a special compact HUD for an interactive right-quarter Viewscreen sidebar. The host selects one eligible Console under exclusive authority. Couriers place Command there by default.

The ordinary journey is select a scenario/ship, join through the browser route, claim an eligible seat, choose depth, ready and enter play. Missing seats use ordinary AI. Individual practice and short shared exercises are optional; completing a tutorial is not a condition of joining live play.

Human-seeking controls offer supported work to eligible people without silently taking another Controller's authority. Their attention belongs in the receiving player's workload budget. A vacant, disconnected or explicitly Away Console operates all Controllers with ordinary AI; inactivity alone does not imply an automatic AFK detector. The selected complexity profile is retained separately from operator availability. Handover atomically changes effective operators, rejects obsolete queued writes and safely releases held inputs while preserving accepted settings and ongoing physical work. Returning humans regain their selected profile allocation, not a stale copy of earlier machinery settings. There is no separate temporary-fill AI or inference of unspoken human intent.

Console knowledge, ongoing jobs and operational settings persist across handover. The incoming operator receives the Console's working context—zoom, filters and inspected record—adapted to their device, while keeping their own accessibility preferences. A closed or entirely AI-operated Console remains simulated, receives facts and sends reports.

Where a scenario continues after ship loss, preserve useful participation through supported pod/team operations or eligible station reassignment; spectating is optional. This does not grant free instant replacement vessels. Player participation is distinct from the survival of a simulated officer.

## 4. Authority, knowledge and assistance

### 4.1 One controlling boundary

A Station hosts Consoles; each Console owns Controllers. Each Controller owns one command section of its Console blackboard and has one effective operator, human or AI, selected through the Console complexity profile and operator availability. Both operators submit commands through the same admission boundary. Physical Systems consume bound commands rather than hosting their own command AI. Each physical command input has one source; one Controller output may drive multiple compatible Systems. Information display, a request and an accepted machinery command are distinct actions.

A System can combine supported mechanisms consuming Controller-owned command channels such as enabled state, target, attack or psychic interference. A weapon and interferer on one turret can share a target channel while independently enabling effects within actual capability; the physical turret does not gain competing target writers. Two engines can subscribe to one propulsion Controller's throttle, or to distinct Controllers for separate throttles, without different engine code. Distinct inputs such as propulsion demand and Engineering limits may have different Controllers, with an explicit mechanism defining their interaction. Shared physical services, such as a central coolant pump, remain separate Systems with explicit receiving links. Persistent settings are read by every subscriber; discrete actions require identified requests and execution receipts so subscription, retry or replay cannot multiply physical effects.

A fully AI Console uses the same runtime with every Controller AI-operated. A fully AI entity uses that same model for all its Consoles, regardless of player-slot membership. These contracts apply to controllable entities, not only ships: later probes and stations reuse supported mechanisms, observations and Controller policies. No separate player/NPC physics or AI execution path is permitted; implementing those additional entity types is not implied by this agreement.

Connected clients act on one agreed authoritative world rather than independent device-local outcomes. Admission, actual execution and permitted presentation remain distinct across clients and bridges. Each bridge retains its scoped control and knowledge; sharing a scenario or joining a faction does not reveal every ship's internals. Reconnect reconstructs the player's eligible Console state and ongoing work without granting a second controller. Network topology, hosting implementation and payload formats belong to the technical design, while these authority and information guarantees remain requirements.

### 4.2 Actual state, intent and evidence

Keep five concepts distinct:

| Concept           | Meaning                                                                               |
| ----------------- | ------------------------------------------------------------------------------------- |
| World state       | Actual bodies, stocks, conditions, personnel, equipment and work.                     |
| Intent            | Authorised settings, requests, targets and orders, which may currently be infeasible. |
| Execution         | Actual admitted work, its progress and committed resources.                           |
| Console knowledge | Permitted observations, received reports, scoped feeds and retained records.          |
| Presentation      | The operator's current view and deliberate publication of supported information.      |

All Controller AIs on a Console read its permitted observation and accepted-command blackboard, corresponding to the human Console GUI. All Controller AIs on an entity can read its entity-wide blackboard, corresponding to the shared Viewscreen available to humans. Shipwide knowledge is the ship instance of this entity-wide scope, not fleet-wide visibility. Individual Controller AI memory remains private. Co-hosted Consoles do not merge knowledge. Cross-Console feeds and Coordination deliver supported information explicitly; an authored scoped feed can update automatically without repeated Share clicks. Importing evidence never transfers control authority.

Observation producers alone write factual state. GUI actions and AI actions write only authorised Controller command sections through validation, never observed outcomes. Both readers use the same permitted observation outputs, including machinery telemetry and contacts; neither receives a privileged parallel world-state feed. World state remains authoritative for physical execution. Command-board values express accepted intent, and action receipts distinguish admission, execution and outcome.

Observation/report records preserve source, time, uncertainty and scope. A repair work list updates through received reports and team messages, rather than hidden inspection of current machinery. A claimed successful rescue is not physical proof of delivered personnel. Completed work does not become known remotely without a supported observation or report.

### 4.3 State trees and reusable work

Each Console-owned AI Controller uses a configurable, reusable state tree over typed Console/entity blackboard variables, accepted commands, addressed messages and private memory. Its actions submit writes to its own command section or supported messages; it cannot mutate physical components or another Controller's section directly. Definitions support parameter and structural inheritance and reusable reference calls. Each running instance retains its own state and memory independently of the number of subscribed physical Systems. A caller can interrupt a referenced decision tree; this changes intent, not already completed physical work.

Use deterministic authored evaluation order. Where active tasks write the same supported output, later writes win; later evaluations can read earlier writes in that evaluation. This is an authoring/debugging contract within a Controller evaluation, not permission to bypass command-section ownership or produce duplicate physical effects. Controllers read stable permitted snapshots; commands commit before physical input consumption, resource resolution and execution. New physical observations and routed messages become available at the next decision boundary, rather than depending on Controller iteration order.

Suitable finite activities share a job lifecycle: owning System, readiness, commitments, progress, interruption, cancellation and outcome. Each job has an independent execution instance and activity-specific rules. Controller AI decides to request work; job workflows perform admitted work. Reusing the tree editor/evaluator does not merge their knowledge or authority. Reusable subtrees are supported; a separate parent/child job lifecycle is outside the initial job framework.

Queueing expresses demand. Discrete resources are secured for the next executable stage, not for the entire future queue. Each indivisible step secures its complete required set together; failure leaves no new partial claims. Real supplies and completed preparation remain real. Reprioritising waiting work does not interrupt active work; supported cancellation or a physical cause does that. Continuous flow need not be represented as a job.

## 5. Flight, passage and operating geometry

### 5.1 Local flight

Capital ships operate on a plane, with limited vertical displacement for ship-to-ship collision avoidance. Small craft and torpedoes use full 3D movement. This distinction is physical, shared by humans and AI, and independent of station complexity.

Helm's defaults are desired speed and desired absolute heading. Acceleration and turning respect actual actuator, energy, heat and damage limits. Both assisted and manual flight first passively damp current velocity toward its closest point on the segment from zero to desired velocity, then apply each funded engine�s acceleration along its own hull axis, capped at that axis�s requested speed. The combined desired velocity follows actual hull heading and deliberate lateral intent; the main and lateral engines retain independent acceleration and power limits. Passive damping remains active without working or powered propulsion, costs no energy or heat and never adds speed; its separately authored deceleration rate defaults to nominal acceleration. The existing launch acceleration curve remains. Steering does not automatically follow contacts or maintain broadside alignment. A visible HUD toggle offers direct steering, thrust and fitted lateral control. Direct yaw and forward/reverse thrust support keyboard, assigned gamepad and a spring-centred virtual joystick. Manual forward/reverse and lateral axes request proportional desired velocity; releasing input requests zero and passively brakes toward rest. A single stick maps horizontal movement to yaw and vertical movement to thrust. Boost can be sustained or released within equipment limits; releasing it removes extra thrust, not momentum.

Match Speed persistently matches the selected target's observed speed. Throttle input or toggling it off cancels matching; steering alone does not. When observations become stale it holds the last observed speed with clear feedback, and resumes updates if the same contact is observed again. It does not maintain relative position, range or aspect, follow unseen manoeuvres or substitute a different target.

Waypoint, route, escort and formation assistance remain supported equipment/AI capabilities. Optional multi-leg routes show the current leg, while simple travel needs one destination. Publishing a route does not engage autopilot or take steering from a human. Formation slots use each ship's ordinary delegated movement AI. Manual flight controls must preserve clear authority and cancellation feedback. Formation does not eliminate each vessel's individual equipment, knowledge and readiness requirements.

Fitted boost Systems modify selected equipment properties while held: initially maximum forward speed, acceleration and turn rate. Bonuses are authored multipliers with separate boost power/heat costs; target equipment still needs its own operating power. Multiple boosts use the strongest paid bonus per property. Release, input loss or Controller handover removes the bonus without erasing momentum. Boost is suppressed throughout impulse charging and transit.

Lateral engines can provide port-only, starboard-only or bidirectional thrust. Two held controls command ship-relative movement; holding both means neutral. Release removes the lateral velocity request; passive damping then arrests drift. An AI-owned lateral Controller prefers feasible lateral collision avoidance using permitted navigation evidence and reported actuator capability, including when a human retains forward propulsion and steering on the same Console. Passive damping handles ordinary drift correction; the lateral Controller reports collision-avoidance intent through Controller messaging. Complexity profiles determine ownership per Controller; human ownership prevents AI intervention.

Impulse requires an authored minimum reserve in its connected local battery before charging. The threshold covers total charging energy and fits the battery's capacity. Energy is spent as charging progresses; insufficient energy pauses progress, and cancellation discards progress without refund. Completed charging enables continuous forward transit on the following tick, multiplying explicitly bound propulsion speed and acceleration. Steering remains normal during charging and is reduced by an authored fraction during transit. Manual cancellation, collision, disablement or insufficient operating power ends transit. Ordinary limits return without clamping velocity; subsequent braking and collision response use the ordinary physical rules.

### 5.2 Guild passage

The initial [Navigator FTL departure](ftl-departure.md) increment provides a dedicated Navigator Console for leaving a mission. Its initially empty reserve takes substantial power to charge, wastes power as heat while held ready, and gradually discharges as heat when switched off. A fully charged operational drive permits deliberate departure; an active displacement transfer must finish first. Deployed personnel can be left behind with a visible warning. Departure ends that crew's mission and freezes a report of its known objectives and personnel, while other ships continue. This increment omits destination selection, fuel and qualified-officer gates; the full passage requirements below remain the subsequent design baseline.

Science controls the navigator and coordinates with other stations. Passage consumes actual equipment capability, navigation knowledge, energy and other authored supplies. Interstellar travel uses finite transit fuel. Routine local manoeuvring does not add a separate propellant chore.

Interstellar passage, fitted local jumps and short-range displacement share **one active passage operation per navigator**. Preparation may overlap. Science considers competing requests and releases an operation at the appropriate moment. Ordinary delegated navigator control can execute within approved intent and limits; it is not a requirement that a human approve every passage on an AI-operated ship.

Readiness shows actual prerequisites as known through legitimate observations: route/destination knowledge, apparatus condition, positioning, power and relevant payload/shield requirements. The crew coordinates verbally. There is no universal ready-button approval round or hidden unanimous permission gate.

An urgent request does not pre-empt active passage. Science either lets it finish or deliberately uses an equipment-supported abort. Abort consequences follow the operation's authored stage and physical commitments; no universal arbitrary lethal mishap is added. A request or readiness display never promises that conditions will remain valid at execution.

Crews can assess and attempt uncharted routes. Routine successful travel is compressed; practical route failures, occasional local disturbances and meaningful onboard work can justify playable transit. Compression must advance elapsed world time and relevant ongoing consequences consistently; it must not silently complete protected choices, discard obligations or create free servicing. Keep interruption boundaries and arrival circumstances legible.

Each formation member requires its own functioning drive and navigator. Specialist local-jump equipment is not assumed on every hull and does not universally require a receiving anchor. Fitted drive profiles differ in accuracy and gravity-well sensitivity: randomise an arrival within the supported radius around the intended point, then displace it outward from large masses along their gravity wells. These effects must follow authored limits and usable feedback rather than guarantee the intended point. Interstellar messages travel aboard vessels; transit compression must preserve their actual delivery sequence.

### 5.3 Displacement and external geometry

Displacement transfers a bounded volume containing a small team or compact load over short range. It requires a sufficiently established suitable destination but no universal receiving pad. Conventional scans can sometimes establish an interior destination. Unknown interiors do not become selectable merely because displacement is fitted. Shield sectors obstructing the transfer must be lowered; other equipment, interference, geometry and resource limits still apply.

Tractors are engineered force projectors. Suitable equipment can influence shielded or resisting vessels within actual force and operating limits. A tractor release does not intrinsically abandon deployed personnel or equipment. Repair, transport and remote support have their own commitments and cannot inherit that safe-release assumption.

Docking uses supported approach, physical mating and a short visible separation process. Movement input can return authority from assisted approach to Helm. Emergency release requires suitable hardware and stated consequences; ordinary undocking does not carry a universal damage tax. Transfers and people remain where actual work has put them when departure interrupts service.

## 6. Sensing, investigation and evidence

Radars may have an authored outer detection band that supplies bearing-only blips without range or new identity details. These contacts remain selectable for focused scanning and weapon assignment. Focused acquisition obeys the scanner's own range and coverage; weapons can fire along the observed bearing, with suitable missiles acquiring independently through their bounded seekers. Earlier findings remain dated evidence.

Relevant operational radars show basic detected returns. Detection does not automatically establish identity, intentions, internal condition or a firing solution. Science supplies deeper interpretation. Captain coordinates through crew conversation and the ship's published Objectives list.

Broad and focused sensing are separate Systems with separate power demands. Broad sensing performs routine progressive assessment where evidence permits; focused work investigates chosen subjects and optional supported questions. Fitting more capable equipment creates real capability, not a physics difference between Console complexity profiles.

A fitted focused scanner has one active enquiry with a prioritised queue. Separate scanners can work concurrently. Intrusive analysis occupies the capable scanner's enquiry slot and can be detectable. It does not guarantee secrets or bypass obfuscation. Evidence can remain insufficient, and supported questions do not guarantee answers.

Independent passive receivers can listen alongside active sensing and have their own resources and controllers. Reorientable directional arrays change actual coverage rather than revealing everything on demand. Held-record retrieval and routine correlation are responsive, without an additional gameplay processing timer or capacity budget. AI-controlled stations take supported reading time before using relevant text. Integrated motion/firing estimation belongs to relevant sensing/fire-control, not a mandatory separately fitted computer System. Restored shared feeds expose retained findings through normal history without a separate catch-up interface or System.

Findings have individual confidence. Position, identity and condition need not share one confidence score. After contact loss, predictions use observed motion, grow uncertain and remain visibly distinct from live returns. They retain the last actual observation for inspection without following hidden manoeuvres. Ambiguous contacts use wireframe/uncertainty presentation without revealing the unknown hull's true geometry.

Passive bearings, triangulation, directional search, probes and competing hypotheses support selected meaningful ambiguity. Routine contacts do not require manual triangulation or an obligatory puzzle. Probes are physical deployables with their own fitted sensing, power and communication constraints. Environmental/resource/anomaly enquiries follow the same evidence model; forecasts cannot read hidden future script events or actor choices.

Routine reference information attaches automatically where supported. Unusual records require discovery and explicit attachment. Library entries are evidence and context, not proof of a current vessel's condition. Received reports can corroborate or contradict findings; the game preserves their provenance rather than forcing a verification checklist for every claim.

Shared Navigation, Science and Tactical feeds are selectable and scoped. Recipients retain independent route and target decisions. Ordinary authored links can supply specific feeds automatically; they do not pool all blackboards. Illumination is an aid, not Science's veto over Tactical target selection or a universal firing permission. Different weapons can use maintained guidance, independent seekers or unguided fire, with support-loss behavior appropriate to the equipment.

Signatures come from a readable set of operating causes, including major machinery and explicit emissions. Cloaking does not universally switch off when firing, scanning or transmitting, but those actions affect detection and can have equipment-specific incompatibilities. It does not erase old observations. A low-emission profile marks risky actions for humans without inhibiting controls or adding repeated confirmations; ordinary AI follows the authorised intent.

## 7. Communications and psychic warfare

### 7.1 Channels, reports and orders

Within a controllable entity, physical Systems retain always-active factual reporters independently of human/AI control. They report legitimate observations, accepted settings and actual outcomes with change detection and hysteresis; they do not editorialise, invent human intentions or issue requests on a human's behalf. Controllers and explicit human actions originate requests and proposals.

Coordination addresses a specific Controller or the whole entity. A Controller-addressed message reaches its AI inbox or its human's Console as text according to the current effective operator. Entity-wide messages enter the shared blackboard and appear on the Viewscreen; important AI-to-AI messages also publish there. Structured metadata and localised text describe the same message. Preserve destination, provenance, identity and history across handover, without making every AI on a Console an actionable recipient. Shared publication exposes the explicit publishable message payload, not unrelated private Console information. These local rules do not bypass inter-entity signal/link constraints below.

Player-crewed ships support text, audio and video communication. AI communicates through text and supported console messages, with urgency and accessible notification cues; there are no AI voices. Human and AI ships use the same in-world channels, producing actual traffic that can be observed and intercepted.

Present concurrent conversations as persistent threads with clear urgency and selected audio/video focus. Comms can address an individual or fixed audiences comprising its own fleet, each faction and all ships; custom arbitrary group construction is outside the selected scope. Dossiers expose terms, evidence and supported status, leaving fulfilment-risk interpretation to Comms instead of automatic risk scoring.

Every ship supports relationship/objective-contextual generic requests separately from plot dialogue. A human receiving operator handles the generic conversation as text chat; AI recipients respond through their ordinary knowledge and policies. Public low-power channels, chatter, encrypted messages and supported decoding create Comms work without fictitious conversations generated from hidden truth.

Comms distinguishes an offer, a claim, a request, an accepted commitment and a verified outcome. Formal obligations are content-specific; not every generic hail creates a contract. Negotiation can establish terms and access, but it does not reserve supplies or command human-held machinery. Behavioural surrender changes orders and intent, not the physical ability to violate terms or immediate ownership.

Structured remote orders use authored validity and supersession. Pending time-sensitive messages can expire or be replaced; durable orders can persist. Expiry of an undelivered message does not cancel work already accepted remotely. Delivery and accepted/refused receipts return automatically through viable links; no acknowledgement chore is added. No receipt means uncertainty, not proof of non-delivery. Completion needs a supported outcome report or observation.

Relays route permitted messages and telemetry through viable equipment and paths. Geometry, solid terrain, fields, fitted resources, encryption and psychic attack affect reach and availability. There is **no bandwidth pool, traffic-volume budget, message-capacity meter or congestion quota**. Gameplay link loss must not be confused with a player's network disconnection.

### 7.2 Psychic attack and protection

Cruiser Science owns Phantom choir and Psychic ward alongside conventional sensing and shields. Veil belongs to Courier Support. One-star Science automatically wards its own ship and leaves Phantom choir inactive; two stars adds direct psychic choices and targeting. The entire legacy EWar family is rethemed as psychic operations, including jamming, deception/spoofing, hardening, bounded access/disruption and the relevant countermeasures. These are effects on actual supported signals and equipment, not unrestricted narrative permission or a network-security simulation.

A choir has multiple Duty Officer seats. Each present healthy specialist unlocks one authored power, and several powers can run concurrently through the same apparatus and actual resources. The first powers are cloak, ghost-contact scrambling and a ward for one ship, self or a known ally. Overlapping copies use the strongest effective contribution. Detailed scans or damaging shots through a ghost dispel it for that observer; it fades rather than exploding and cannot immediately reappear within the same activation. See [Psychic choir](psychic-choir.md) for the authoring and continuation contract.

House-trained specialists and fitted apparatus constrain availability, geometry, target scope, energy, heat, duration and counterplay. Suitable interference uses the same applicable mount/arc/aiming principles as other equipment. Optional decoys are ordinary physical configured entities with finite capability and life, rather than false ships silently inserted into physical state.

Targeted interference can track a chosen contact while affecting other craft inside its actual coverage, including friendly functions. Protection is finite and prioritised at System level, or by Station in a simplified preset that maps to the same underlying Systems; do not require component-by-component allocation. Routine decisions remain with ordinary Console Controllers; physical Systems consume their bound commands.

Remote access can expose supported information, open a supported access path or interfere with a capability. It does not grant command of an enemy System. Psychic perception concerns the deep and does not reveal arbitrary intentions, interiors or objective truth. Conventional interception and decoding remain evidence operations; intercepted enemy text in team competition can be meaningful. Interception does not automatically defeat encryption, and some messages require better capability or acquired keys rather than enough waiting.

## 8. Combat and protection

### 8.1 Engagement choices

Combat combines detection, engagement geometry, defence, endurance and external objectives. Hulls and doctrines can favour different ranges and styles; a compulsory missile-to-PDC-to-broadside sequence is not imposed. Orders and observations can telegraph preparations without revealing exact hidden targets or timers.

Tactical selects a primary offensive target and supported separate weapon-System assignments. Prepared fire groups come from authored or saved configurations. Individual overrides can de-group a System during play; group commands must not overwrite that override. Live barrel-pattern programming and routine emitter administration are not required.

Arcs, range, traversal, tracking, relative motion and weapon-specific solution quality affect actual opportunity and effectiveness. Ordinary weapon AI handles routine aiming/lead and authorised release. Science's designations remain advisory. Some equipment permits staged preparation and chosen release; there is no universal bridge-wide ready/fire rhythm.

### 8.2 Configurable weapon families

| Family                    | Supported distinction                                                                                                                                  |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Sustained beams           | Operator-controlled duration within geometry, local reserve and thermal limits; retain specialist beam variants rather than inherited phaser branding. |
| Kinetic/projectile banks  | Ammunition-fed projectiles or supported energy bolts, with meaningful lead, travel, volley and resource differences.                                   |
| Guided missiles/torpedoes | Finite ammunition, physical flight and weapon-specific seeker/guidance dependence.                                                                     |
| Broadside batteries       | Hull positioning and exposure with authored firing arcs, not a required fit on every ship.                                                             |
| Point defence             | Finite coverage, tracking, readiness and interception priorities; saturation is a real competing-demand problem.                                       |
| Long-range kinetics       | Authored geometry and preparation distinguish them from other banks without universal hit or accuracy rules.                                           |
| Payloads and mines        | Supported explosive areas, penetration, marking and disruption effects; physical persistent mines where fitted.                                        |

One supported weapon composition model expresses families, effects, projectiles and firing patterns. Penetration and deposited damage/effects are separate properties. EMP-like equipment disruption uses temporary component debuffs that refresh, alongside a longer stacking resistance buff. Reapplication retains the greater of the remaining duration and the newly resistance-adjusted duration. Temporary disruption expires without requiring repair by default. Its V3 fiction must follow the psychic-operations agreement where the mechanism belongs to that family.

Suitable guided ordnance accepts post-launch instructions over actual viable links; independent-seeker and fixed-intent variants remain valid. Preserve the payload's physical properties at launch rather than allowing a later order to change its material load. Shared finite loading capacity can serve authored launcher groups under priorities, while independent loaders remain possible. Suitable PDCs can attack ships/craft as well as intercept, competing for the same tracking, ammunition and thermal capacity. Selected heavy guns produce actual recoil; supported assistance can compensate but cannot erase its physical origin.

Area effects apply to physically exposed eligible entities regardless of faction, so firing policies manage collateral rather than granting allies immunity. Marking payloads deliver physical beacons that observe and communicate while capable; they are not permanent perfect-track bonuses. Mine discrimination depends on locally available sensing and authored trigger rules; simple indiscriminate proximity mines are also valid equipment.

Magazine stocks, loading, launch commitment and fabrication are actual work. Recipes consume finite material, energy and time. Delegated production can maintain authorised stock goals without repeated batch clicks. A launch cannot be reversed by cancelling a queue entry after the projectile exists.

### 8.3 Protection and counterplay

Science operates directional shield priorities, redistribution and retuning; Engineering supplies power/coolant, Helm chooses exposure and Tactical selects offensive opportunities. Shield generators can retune while raised through a timed transition that temporarily reduces protection and costs energy/heat. Retuning is universally available as a shield-generator capability, with equipment differences in performance.

Directional protection is separate from the generator's local battery energy. Hits deplete the affected arc's shield strength. Raised fields use energy continuously, with most shield consumption arising from recharging protection. Redistribution is battery-mediated: discharge protection from a donor arc into the battery, then spend that reserve to recharge another arc faster than incoming reactor flow alone permits. It is not instantaneous direct arc-to-arc transfer, and the donor really loses protection.

Optional suitable scanners can probe shield response. Findings usually persist for the observed configuration, without continuous frequency matching. Benefits are modest in many engagements but can matter greatly with particular shield/weapon combinations. Configuration changes can require renewed evidence; knowledge of them follows observation.

Incoming attacks can be intercepted, evaded, disrupted or endured through fitted capabilities. Transit interdiction and formation benefits require suitable equipment and counterplay, rather than a universal combat lock that forbids escape. Remote support can supply compatible energy or shield assistance using real source/recipient limits. Physical repair emitters are excluded; teams and equipped work craft do that work.

## 9. Energy, heat and material supply

### 9.1 Generation, local reserves and thermal limits

The reactor has commanded output, finite response, local heat, coolant use and a capacitor reserve. That capacitor is the default main supply buffer; consumers have local batteries through the same storage model. No mandatory third ship-wide battery or extra weapon-capacitor layer is added. Extra storage can be fitted where authored.

Energy-consuming Systems operate from actual local reserves and receive energy through finite connections. Reserve fullness affects passive loss; charging and operation/depletion generate authored heat. Incoming energy is limited by actual consumption, passive loss and available headroom. Unaccepted energy is not drawn from the source. Unused reserved incoming flow does not automatically redistribute elsewhere.

Owning operators choose equipment output, boost or overdrive where fitted. Engineering controls supply flow and cooling. Increasing supply alone does not increase weapon damage or thrust. Overdrive raises energy/heat and supported stress; it is not a universal random-damage mode.

Ordinary equipment protects itself at authored thermal limits. Supported owner-controlled overrides can deliberately accept increased risk. Below a lower recovery threshold, an ordinary thermal stop clears automatically and operation resumes only as current orders, enabled state, preparation, resources and authority allow. Recovery does not undo a deliberate shutdown or repair faults.

An empty local store reduces/stops continuous output or delays an unaffordable discrete action; it does not add a generic restart lock. Prepared reserves provide intervention margin. Forecasts state their assumptions and describe actual resources rather than adding an abstract endurance bar.

### 9.2 Distribution and coolant

Engineering's coarse groups allocate available flow; ordinary distribution AI adapts within each group's budget using demand and reserves. Detailed groups subdivide that understandable coarse organisation. Healthy allocation changes do not gain a second artificial preparation ramp beyond actual equipment response.

When requested group flows exceed supply, supplied allocation groups are eligible for random flow cuts, including life support. This is a cut of incoming allocation, not an order to shut down the connected System or reactor. Remaining local reserves supply response time. Stored manual flow values remain unchanged. Restoration is automatic by random choice among cut flows that current supply can sustain, with appropriate hysteresis; do not substitute waiting-time or priority ordering, or evict supplied flows merely to rotate service. Cuts and pending requests are clear. There are no hidden critical-group exemptions. This electrical-flow rule is distinct from first-eligible material-stock allocation.

Allocated cooling reserves available heat-removal capacity. It spends coolant only for actual heat removed, not merely because capacity was assigned. Reserved capacity remains unavailable elsewhere until reallocated. Passive heat rejection is an authored equipment property, without a separate radiator-deployment control loop.

Optional recycling equipment collects/handles used coolant and recovers a fraction through finite capacity, time and operating costs. The loop has net consumption; reclaiming coolant does not dispose of heat. Ordinary recycling AI runs routine work without repetitive batch commands. Collection/storage compatibility, yield, power/heat costs and throughput are authored tuning, not an assumption of universal recovery.

### 9.3 Stocks and deliveries

Main stores occupy compatible compartments; Systems retain local stocks and target levels. Engineering owns main-hold reserves and external supply readiness. Local operators retain local stock targets, and routine reachable replenishment is automatic. Routine supplies are aggregated; important consignments retain decision-relevant identity, ownership, origin, condition or hazard properties.

**The first eligible authorised request receives scarce available main-hold stock.** Eligibility includes permission, compatible stock/capacity and applicable holds. Engineering can hold stock or cancel waiting work; a later request does not silently steal committed resources or cancel another deployment. Do not substitute automatic importance-based priority reordering for this rule. Ties require stable implementation ordering, not a new gameplay auction.

At dispatch, an aggregate batch leaves source stock and reserves compatible receiving space. An undispatched request reserves no destination space. Arrival deposits the batch; blocked routes, isolation or damage can strand it in transit. Preserve its real quantity, location and progress for waiting, rerouting or recovery. A cancelled destination does not teleport stock home or refund consumption.

Internal delivery uses authored throughput/concurrency, routes and resources rather than a separate porter team for every batch. Team/delivery routing follows compartment and inter-deck connections with activity-specific access and safety. Automatic routes respect deliberate seals and request an authorised change instead of silently reopening them. Resource conduits and penetration paths are separate relationships from walkable routes.

All inter-location resource deliveries in the active sector use physical vessels, including routine trade. Outside-sector boundaries remain scenario-defined; this does not require a galaxy-wide transport simulation. Supply, repair, evacuation and facility services depend on actual arrival and stocks.

Allied ports can replenish to targets automatically when authorised. Where trade is required, a player initiates a transaction from an editable needs-populated order. House home-base supply uses requisition and actual stock, not an infinite reset. Comms negotiates terms; Engineering manages material readiness; Tactical controls necessary craft commitments.

### 9.4 Conversion and finite work

Shared batch/continuous recipes express fabrication and suitable recycling. Inputs, outputs, storage, operating costs and work are explicit. If a required stored output cannot fit, batch work waits and continuous conversion slows/stops without discarding completed stock. Deliberately changing recipe discards unfinished work, retains compatible unused inputs and completed products where storage permits, and disposes of excess that cannot be retained. Normal logistics performs actual transfers.

Salvage recovers actual surviving cargo, components or materials. Cannibalisation deliberately sacrifices suitable functioning donor equipment and requires access, work and compatible yields; it cannot silently strip another human's active machinery. Mining and construction are equipment-supported optional work with meaningful exposure and alternatives, not mandatory production progression.

## 10. Damage, restoration and loss

### 10.1 Physical model

Ships have multiple decks, each a 2D compartment layout. Systems have explicit Controller-command input bindings and physical equipment in a compartment; control bindings and physical containment are separate relationships. Physical parts provide one or more mechanisms and act as condition/repair targets. They have no separate structural HP or precise internal coordinate; the compartment owns material properties and structural HP.

For ordinary incoming penetration, select a weighted random deck independently of shot Z height, then distribute supported penetration lines across the facing profile according to weapon accuracy/spread. Trace each line along its incoming planar direction through that deck's compartments rather than always drilling the centre line. Charge resistance by distance through material. Separate penetration from delivered damage/effects. As compartment HP falls, its effective resistance progressively falls. Scanning can identify equipment's compartment and support angle advice, but cannot guarantee that a hit chooses its deck or avoids intervening compartments.

Traversal must not apply the entire payload repeatedly to every compartment or component. Supported deposition/attenuation rules conserve the attack's configured effects as it penetrates or stops; their equipment values and implementation remain separate from target selection.

Deposited amount/type and component susceptibility determine eligible effects, chances and severity. Select affected components from the compartment-wide eligible pool; tiny hits need not always cause faults. Conditions alter actual capability: an impaired input link differs from a ruined actuator. Components do not become generic System percentage-damage meters.

At zero compartment HP, a condition disables its contents. Structural restoration removes that condition at a modest authored threshold above zero and below full HP; it reapplies only if HP returns to zero. Other faults remain. Component repairs may proceed alongside structural work where access, safety, parts and team conditions allow, but restored components remain unavailable while compartment disablement persists.

### 10.2 Diagnosis and restoration

Operators and ordinary AI receive supported symptoms, readings and reliably detected faults. Ambiguous causes require observation-only tests or on-site investigation. Diagnostics do not intentionally interrupt or alter the capability being tested. Obvious faults need not be hidden, and AI does not obtain private true diagnoses unavailable to its human counterpart.

Affected Systems report needs through normal communication, including when their Console is wholly AI-operated. Repair maintains a persistent evidence-led work list. New reports and team completions update it; old completion messages cannot erase later faults, and a patched component is not reported as fully restored.

Teams automatically route to eligible work. One team performs a particular component task; multiple teams can repair different components of the same System concurrently. Actual parts, access, time and equipment constrain work. Full repair is preferred where feasible. Teams automatically fall back to a supported patch only when full restoration cannot be performed. A patch lasts indefinitely at reduced effectiveness, does not expire and does not spread; it remains vulnerable to new damage.

Most components can be replaced in the field given suitable means. Larger catastrophic losses can remain irreparable. Compartments can receive full field structural restoration where actual work/resources permit. Major refits still need suitable facilities; no universal weaker-patch ceiling applies to all field repair.

### 10.3 Containment and failure margin

Some authored conditions escalate or spread through shared causal rules; others are stable. Ordinary healthy operation does not generate inevitable maintenance chores. Hard use, overheating and supported adverse conditions create stress-dependent wear. Operating choices can prevent it; known wear uses ordinary repair work.

Doors/hatches have one actual state shared by applicable access and containment. Commanded closure is distinct from successful sealing. Breakers and fitted alternate routes can be operated remotely; creating or modifying a bypass requires team access, materials and work. Console rerouting cannot create absent hardware.

Compartment atmosphere, breaches, isolation, suppression and venting use coarse conditions and reserves rather than a fine fluid model. Intact areas can remain usable while damaged areas become unsafe. Prepared survival margins and warnings permit intervention; there is no universal ship-death countdown. Selected mission-critical work can impose movement constraints, but routine healing and ordinary maintenance do not continually demand that Helm stop.

### 10.4 Disablement, abandonment, capture and destruction

A compartment reaching zero HP disables its contents. When every compartment has zero remaining structural HP, the ship is destroyed; its authored destruction behaviour controls the explosion presentation. Other supported catastrophes may also destroy a ship. Recoverable disablement, abandonment, catastrophe and an observer's assessment that an opponent is out of action are distinct states. AI assesses available evidence and doctrine rather than waiting for an explosion or reading hidden components; later recovery can change that assessment.

When humans are aboard, a human initiates abandonment. Ordinary delegated steps order non-hostile standby while retaining supported evacuation, life support and distress functions. Fully AI-crewed vessels can decide through ordinary policies. An authorised crew can restore the same abandoned ship to service during the scenario through actual personnel transfer and repairs, preserving its identity and condition; abandonment alone imposes no port-return requirement.

Supported capture requirements transfer whole-ship operational control. Securing a room or sabotaging equipment does not grant partial command of an enemy System. New authorised operators can access already-readable retained Console records with their original scope, source, age and uncertainty. Capture does not reveal encrypted content, private AI memory or unavailable ongoing feeds.

Self-destruct is a distributed operational procedure with a visible cancellable countdown and legible blockers. Humans initiate when any are present; human-held Systems retain their operators' steps, while ordinary AI carries out authorised steps on AI-controlled Systems over a short visible period and reports them. There is no vote/quorum substitute or automatic all-station command takeover.

## 11. Personnel, craft and away missions

### 11.1 Finite people and capability

Anonymous personnel are aggregate typed teams. Stable authored competencies can overlap; routine job-by-job speciality conversion is not part of the game. Facilities use the same team concepts rather than a separate individual workforce simulation.

Named Duty Officers add explicit expertise bonuses to compatible Systems, teams or missions. Basic teams and competent assistance work without them. Reassignment has coarse actual travel and bonuses start on arrival; away personnel do not teleport between duties. Genuine machine minds are not the default explanation for assistance.

Duty Officers belong to authored departments, initially Command, Sailing, Tactical, Engineering, Sciences and Signals. Each department is managed by one Station; smaller ships combine departments under fewer Stations. Stations can view and reassign only their own officers, with no interdepartmental or ship transfers. Controllers and teams have separate officer slots, one duty per officer. Controllers have compartment locations for travel but are not damageable. Officers support both human and AI operators; compatible traits use the additive-positive/reciprocal-negative modifier formula documented in [Duty officers](duty-officers.md). Personnel shows assignments, reserves and travel. AI internal messages retain the sending officer's name/portrait, or a generic crew portrait when unstaffed.

Stuns suppress officer expertise and qualifications for 20 simulation seconds. Minor injuries permit baseline duty but suppress officer expertise and qualifications until treated; severe injuries make an officer unavailable. Medical teams heal minor cases and stabilise severe casualties; finite medical facilities complete severe recovery after actual patient transfer. Treatment uses team time and facility capacity; medical consumables are deferred in the first implementation. Permanent losses occur only in explicitly lethal significant damage events when the exposed System or compartment was already heavily damaged. Casualty checks follow discrete significant events, not arbitrary repeated damage-tick rolls. See [Teams and Medical](medical-teams.md) for the implemented authoring and persistence contract. Routine injury does not add automatic deterioration or repeated movement restrictions. Qualified relief personnel can be authored aboard a ship; complement size does not imply an unlimited pool of every qualification.

Science owns onboard Medical assessment and treatment priorities. Team skills, treatment resources and safe locations govern feasible care. Clinical and technical specialists supply expertise to external operations; they do not replace the Comms-facing away-decision model below.

### 11.2 Deployment, support and recovery

The [carried-craft contract](carried-craft.md) specifies prefilled bays of ordinary entities, persistent craft condition, docked energy/ammunition transfers, interceptor defence/escort and bomber attack-and-return orders. FTL preparation recalls craft and shuttle parties without guaranteeing recovery before departure. Away missions allow the crew's own ship unless explicitly prohibited; Helm must manually reach the deployment or pickup location. Mission trees check transport capabilities and can release independent teams' shuttles for other work.

Tactical owns security and flight operations: combat sorties, rescue/evacuation sorties and general utility-craft commitments. Routine craft flight uses ordinary individual AI. Particular-craft assignment is an optional deeper decision; goal-level dispatch remains possible.

Standing authorisation permits suitable routine specialist transport requests within actual craft availability, permitted work and reserves. It does not require Tactical to approve every ferry. Conflicting/protected commitments require coordination. A request cannot double-book craft, cancel an existing mission or take another Console's controls.

Carried craft, probes, decoys and turrets are ordinary configured entities with supported equipment, resources, condition and controllers. Launch, servicing, recovery, links and capacity are real constraints. Delivered independent teams can work with locally available tools/parts after the mothership leaves. A shuttle becomes free only if the task no longer needs its tools, shelter or immediate extraction. Finishing a task does not instantly recover personnel; pickup is actual transport with travel, capacity and risk.

Umbilicals can perform equipment-supported parallel transfers under finite throughput and priorities. Remote energy/shield assistance requires compatible receiving equipment. Field repair uses teams or work craft and cannot consume inaccessible mothership stores. Withdrawal can leave people/equipment needing later recovery; completed effects and delivered supplies remain in place.

Security teams also fill ordinary authored away slots. Target hulls author boarding routes, defending security/exotic defences, and Comms decisions for bridge seizure or reactor intervention. Displacement needs shield clearance; shuttle insertion requires total compartment health below half; disposable boarding torpedoes bypass these vulnerability gates while remaining physical craft. Whole-team injuries carry authored effectiveness penalties and Medical can treat them. See [Security and boarding](security-boarding.md) for the runtime contract and current verification.

### 11.3 Away-mission execution and Comms decisions

The initial displacement increment is documented in [Away missions](away-missions.md). On the rescue Courier, Helm owns Dispatch and the Navigation department. A qualified navigator remains at the Navigator duty aboard ship; missions bind the displacement System as transport. The owning Station assigns each transport/team/officer, while Dispatch opens assignments, sends a ready manifest and recalls parties in range. Several missions may bind one displacement System, with one active transfer at a time. Engineering contributes repair teams through its Away missions panel. Attached officers accompany teams. Authored recovery branches preserve completed work and choose resumption checkpoints. This Courier layout supersedes the earlier Science/support placement for that hull.

Away missions are authored state trees. They execute routine work autonomously and occasionally present a consequential decision to **Comms**. The bridge's preparation, personnel, equipment, scans, access, protection and support change what is feasible and the applicable chances. Not every assignment needs a mid-mission prompt.

A decision has an authored timeout and default. A received answer selects a supported branch; without an answer the default resolves it and the tree continues. Link loss does not freeze the operation or introduce a separate return-home policy: the team continues through the same tree and defaults unanswered decisions after their timeouts. The bridge learns what happened through communication or observation; it does not gain live remote knowledge because the host knows the state.

Checks show exact percentages and contributing known factors. Those odds describe the stated check, not guaranteed mission success. Unknown circumstances are not secretly added to an advertised roll: genuinely new circumstances must become a changed situation rather than covertly falsifying the presented chance. Save/resume preserves resolved outcomes and pending decision timing, preventing rerolls, duplicated branches or expired questions reappearing as fresh decisions.

Boarding/internal defence uses whole-team objectives on supported ship locations. Teams route and resolve encounters automatically within knowledge, access, equipment and support constraints, rather than individual tactical movement. Human controls focus on commitments, supported interventions and recovery. Escape pods transmit supported distress/beacon signals where possible; finding and recovering them still needs evidence, viable equipment and physical work.

## 12. A world that continues working

### 12.1 Actors, services and relationships

Civilian and other outside actors pursue bounded purposeful work such as deliveries, repairs and evacuation. Routine traffic is managed by local services; crew intervention concerns consequential exceptions, emergencies or deliberately traffic-focused scenarios. Reinforcements usually require a cause, dispatch and travel that can sometimes be observed, prevented or intercepted; appropriate authored timed arrivals remain possible.

Facilities share applicable shield, System, component, stock and team rules with ships. Their machinery supplies finite services. Depletion, breakdown and repairs propagate through supported dependencies automatically. Stocks, deliveries and contextual exchange matter without a general market-price simulation.

Recognisable doctrine and bounded variation affect actor priorities, risk tolerance and reserves through ordinary individual AI. Faction relationships coexist with actor-specific trust based on known interactions. Neither reputation nor actors' decisions update from undisclosed events by magic. Human opposition predominates; ancient or non-human threats are unusual.

Allied requests, surrender, coercion and pursuit depend on objectives and supported behavior. An actor protecting a base may stop chasing early; a determined hunter may pursue through travel if its knowledge and capabilities permit. There is no universal leash or guaranteed allied obedience. Complementary multi-bridge goals preserve each crew's local authority and worthwhile work.

### 12.2 Terrain and fields

Planets, moons, satellites and asteroid fields are static reference bodies; orbital motion and scheduled orbital windows are not required. Moving objects move through supported ordinary motion. Selectively clearing terrain can change access or exposure, and that clearance persists long enough to matter. Cosmetic rocks are not automatically cover.

Fields have bounded authored spatial properties, falloff/evolution and selected causal changes. Some hazards offer concealment or advantageous approaches as well as costs. Solid terrain and fields can obstruct/attenuate applicable sensing and communications along the path, even if endpoints are outside the affected region. Local exposure effects remain separate.

Anomalies support investigation, resources, local effects and selected route-changing phenomena. Crews can sometimes discover or change usable connections; there is no universal instant-death black-hole rule. The selected coarse planetary mode uses a wrapping planar map for suitable surface/orbital-adjacent operations and points of interest, without introducing actual orbital-body motion or on-foot control. Exact geography and eligible mission sites belong to later content authoring.

## 13. Scenarios, modes and continuity

### 13.1 Objectives and causal consequences

Objectives describe outcomes and constraints, leaving humans free to choose supported methods. Optional procedures guide ordinary AI without becoming a new omniscient crew planner. Objective recipients, actual physical completion and completion credit are distinct; independently scoped objectives do not all complete merely because one actor changed the world.

Meaningful transfers, deployments and interventions normally establish irreversible allocations through actual authorised work. Explicit protected mission choices remain where needed; AI must not silently choose a protected beneficiary merely because it can operate the transport. Missed intermediate goals normally change options and conditions. Some authored failures can end a scenario, but expiry never manufactures successful delivery or selects an unchosen beneficiary.

Reusable causal rules can combine where their prerequisites actually hold. The crew may solve a problem through a supported approach not individually scripted. Claims, offers and plans remain distinct from true state and accepted obligations. Recaps and debriefs report crew-known outcomes and evidence-supported explanations, preserving unresolved uncertainty. Full causal diagnostics are author/test tools.

### 13.2 Mission composition and pacing

Scenario state trees execute progression. Reusable situation instances have independent state and communicate through declared events/contracts, not private-memory access. Supported sandboxed scripting can extend scenario composition within authorised effects; the V2 scripting language is not a V3 technology commitment.

Partial-order hierarchical task planning composes parameterised activities from imported authored libraries. It proposes feasible work using distinct ship capability, human attention and shared physical-resource forecasts. Physical execution remains with authorised Systems and jobs. Predicted effects are not facts and planning reservations are not actual stock claims.

A bounded opportunity search can discover useful combinations from existing needs, actors and capabilities—such as repair followed by evacuation—using shared activity operators. It revalidates candidates and reveals selected opportunities through normal sensing, Comms and objectives. There is no omniscient job board, and an offered activity is neither acceptance nor a command to human machinery.

The Director paces unannounced work within authored bounds, considers continuing obligations and downweights recent repetitive problem/solution patterns. It prefers useful existing entities to gratuitous new setup. Stable ordering and finite candidate/depth/expansion limits keep generation bounded and reproducible. Search exhaustion leaves existing execution intact. The Director cannot undo established commitments, secretly move an announced deadline, invalidate earned preparation or infer boredom solely from low input counts.

Larger crews need concurrent meaningful responsibility; enemy strength is balanced separately. Forecast attention at shared human-operator level rather than pretending each hosted Console belongs to a separate person. Workload forecasts are design aids requiring playtest calibration, not automatic proof of engagement.

### 13.3 Modes

| Mode       | Core structure                                                                                                                                                             |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Standalone | Authored starting state, an operational situation, supported methods and outcomes, then a debrief.                                                                         |
| Patrol     | One assigned main priority, with optional work and freedom over supported methods. Works as a single session or continuing run.                                            |
| War        | An authored sequence/count/deadline bounds a decisive finale; earlier bridge operations alter its conditions. Allies and coarse strategic consequences use the same world. |
| Sandbox    | Optional offered jobs and self-directed exploration, recovery, logistics and projects; declining work is valid play.                                                       |
| GM-led     | Any suitable scenario with optional actor performance, direction and facilitation; ordinary activity remains viable without a GM.                                          |

Territorial change follows operations such as supplying, securing, repairing, defending or negotiating over facilities/routes. Outside actors handle ordinary development. There is no separate player colony-budget or production-queue layer.

### 13.4 Persistence and session usability

Compatible save/checkpoint/resume preserves the same mission's world, identities, conditions, inventories, jobs, teams, custody, commitments, knowledge, pending decisions and necessary controller/progression state. It must not duplicate rewards, restart work for free, reroll outcomes or erase deployed assets. Restoring an exact mission is distinct from advancing to a later episode.

Continuing modes preserve relevant supplies and component conditions until actual servicing or explicitly supported downtime changes them. Captured/recovered vessels remain the same damaged and equipped assets with real crew/capacity needs after their originating situation ends. Refusal, abandonment and destruction leave coherent consequences. Standalone scenarios retain their authored starting state.

Rewards belong to the run/campaign. Fresh games do not require account-level gameplay unlocks; competent assistance is available from the beginning. Personal fitting favourites are preferences rather than equipment ownership. Ship names, presentation and factual service history can persist without an extra earned hull-trait modifier layer.

Joining, reconnecting, leaving, ending a scenario and returning to selection need clear states and recovery feedback. A technical failure must not be presented as a fictional defeat. Loading errors must leave an intelligible recovery route. Export/import and compatibility refusal must preserve the user's existing save. No blanket compatibility with V2 saves, packs or protocols is promised.

### 13.5 Optional GM authority

The Director continues by default when a GM joins; the GM can pause/override pacing or use full manual pacing. Actor performance, supported puppeting, information control and cues use explicit authorised tools. Misleading reports can be authored or GM-supplied with investigable clues, while independently earned evidence remains intact. False reports do not become physical ships.

Provide advisory attention tools, attributed history, supported undo and checkpoint recovery. Advice does not automatically execute actions. Technical rollback and deliberate reversal of fictional consequences are different operations; no universal rewind is promised. Live interventions remain distinct from authoring source content and disposable testing.

## 14. Fitting, authoring and presentation

### 14.1 Fitting and reusable equipment

Authors define hull geometry, compartments, routes and mounts. Players choose permitted compatible fits, not move rooms and topology during ordinary preparation. Most mounts have narrow family/type/size compatibility, with selected broader options. Operational costs, supply and sustainment constrain choices; there is no extra abstract fitting currency.

Manageable authored reactor profiles provide sidegrades and understandable forecasts rather than a fine-grained reactor builder. Field replacement/refit is possible where suitable equipment, compatible parts, access and time permit; major changes need appropriate facilities. Ordinary fabrication cannot reproduce every specialised component.

An optional fitting planner supports per-ship personal favourites, comparison, sharing and disposable rehearsal. Whole-ship favourites seed defaults; station claimants can supply their own fit or favourite, and subsequent whole-ship imports preserve explicit claimant choices. Compatibility, scenario restrictions, stock and actual refit work still govern real installation.

### 14.2 Workshop contracts

Workshop composes supported mechanisms, Console-owned Controllers and command sections, physical input/output bindings, Console complexity profiles, factual reporters, message destinations, links, parameters, conditions, recipes, state trees and presentation. Genuinely new simulation mechanisms/node vocabulary require implementation work; the visual editor does not promise unrestricted programming or privileged AI reads.

Templates can contain components, Systems, compartments and assemblies. Repeated placements have distinct identities and runtime state. Service links, mounted capabilities, information bindings, physical containment and control ownership are explicit different relationships. Independent source capacity must not be duplicated by each consumer's link.

Assemblies can supply default named service connections with permitted local overrides; resolve and validate the instantiated links rather than copying source capacity. Console-owned processing can combine permitted observations and references into useful estimates, but does not gain hidden facts or add a compulsory gameplay analysis timer. Inherited/local structure and unresolved references must be inspectable before Test or export.

Define common typed observations/reports, events, condition/modifier behaviors and supported work vocabulary. Conditions reuse identity/source tracking, duration/removal/reapplication/stacking rules and typed capability modifiers, while their causes and remedies remain domain-specific. Conditions progress through compact supported application, threshold/event and expiry rules; each condition does not run a separate state-tree instance. New reaction primitives require implementation work. Event reactions have scoped bindings rather than hidden callbacks with world access. Physical processing phases/dependencies belong to the implementation; authors compose supported mechanisms and inspect constraints, rather than programming processor order. State-tree and workflow ordering remains an authored behavior concern.

Workshop must expose inheritance/reference origins, validation findings, compatible choices, costs and dependency failures. It supports ordinary document history, export/reload, preview and isolated disposable Test. Test worlds and Live tools do not silently rewrite source packs. Demonstrate two meaningfully different equipment or setting variants through the same mechanism, including human controls, AI operation, authoring, saved state and observation limits.

### 14.3 Console and display design

The ordinary in-game Viewscreen shows the game world, a Settings button and supported shared information/message overlays from the entity blackboard. Entity-wide messages and important AI-to-AI messages have readable text and accessible presentation; shared display does not grant specialist controls or expose private Console records. The world is a flat 2D operating plane rendered in 3D. The default first-person camera sits just above the plane at a shallow downward angle and omits the owning ship. An elevated third-person chase mode includes the ship. Both modes offer eight ship-relative viewing directions; camera mode and direction live on the ship blackboard and are changed through authorable Console controls, initially on Helm. All of the ship's Viewscreen presentations follow those shared settings and its actual position and heading. Free orbit, pan, zoom and developer readouts do not belong on this display. See [shared ship camera](ship-camera.md).

Lobby and running play are distinct states. Once launched, returning to setup requires **Settings > Quit to Main Menu**, followed by hosting or joining another game. Closing Settings returns to the same running display; Settings does not pause simulation. Player Consoles and GM inspection use their own clients or configured native display regions. A single-screen native host chooses its display role in the lobby before launch; in-game role-switch buttons are not part of the Viewscreen.

The default Helm UI uses a vertical throttle slider and a click/drag heading ring around the radar, with an equivalent numeric heading input for keyboard and screen-reader users. A Console assigned to a native monitor appears there immediately during lobby setup, showing its name, description and local Ready/Back controls.

After a player selects a station, the client lobby shows its responsibilities, one-sentence descriptions of its player-facing systems and one sentence summarising automation at the accepted complexity. Every available gameplay tab has help for that workload, opened through a `?` button at the top right below the tabs; its readable, scrollable popup supports keyboard/controller dismissal and returns focus without pausing simulation. See [station help](station-help.md) for the eight shipped stations, auxiliary views and authoring contract.

Consoles distinguish requested settings, actual output, known faults, blocked work and stale evidence. Partially functioning Systems show which capabilities remain, rather than a misleading single healthy/failed label. Known faults and repair reporting remain available without opening a human GUI.

Prototype data-authored screens with reusable rectangles and parameterised circle-cutout regions, including corner gaps around a radar. Free-form outlines and arbitrary-region packing are outside that initial prototype. Use shared responsive fallback when shapes no longer fit: smaller devices can reflow to readable panels/lists without changing authority, information or essential controls. Preserve action order, readable labels and usable hit areas rather than clipping or indefinitely shrinking controls. Do not require a desktop layout or precise geometric tapping for time-critical actions.

Deliberate Viewscreen sharing follows the publishing operator's current browsing/selection until sharing ends. It is not an independently pinned topic by default. Radar and navigation views each provide an **On screen** toggle. It shows that live view over the normal world scene with a dimmed background. The currently displayed source's next press returns to the normal Viewscreen; a different source replaces it immediately. The latest accepted explicit display command wins, with no queue of previous sources. Live updates from a superseded source cannot reclaim the display. Published views retain supported knowledge and uncertainty, without handing Captain private specialist controls. Authored shared-display bindings determine placement and access; publication is explicit and communal.

Accessibility is personal: readable type, scalable presentation, non-colour-only status, touch/keyboard alternatives, captions/text equivalents and manageable notification urgency. Distinct non-voice audio cues supplement visible feedback. Localisation applies to controls, reports, author-facing content and supported authored text. No essential instruction depends on an optional peripheral, audio channel or venue effect.

Native hosts/clients, multiple displays, custom consoles, lighting/smoke/show control and external input devices are optional adapters to the same supported state and command authority. Their absence or failure leaves the ordinary browser route usable. These are requirements for compatible behavior, not an inherited runtime or networking stack.

### 14.4 Selected technical direction

The selected stack is **C++ + Flecs + raylib**, with **RmlUi player consoles and custom GPU-rendered radars**, **Dear ImGui docking for Workshop/GM tools**, and **Emscripten/WebAssembly** for browser delivery. UI logic and layouts do not form a separate HTML/JavaScript application; a shared host shell and browser adapter provide platform services. RML/RCSS UI assets do not require Ultralight or another embedded browser.

The native target is six 1920 × 1080 displays at 60 Hz, normally one Viewscreen and five consoles, using one spanning borderless window. One simulation serves all screens. Touch contacts and assigned gamepads route to stable Console identities with independent interaction state, rather than global shared UI focus. Player consoles require screen-reader access, including equivalent radar contact selection/actions; Workshop/GM tools require basic accessibility. Localisation applies to both, with tool text-shaping limits explicitly checked rather than waived.

The [technical stack decision](technical-stack.md) defines these boundaries and the first integration proof. Dependency versions, concrete adapters and build configuration remain implementation design. Screen-reader support, simultaneous physical input and the display performance target require actual smoke evidence; none is claimed delivered.

## 15. Validation scenarios and acceptance

Controller architecture acceptance must exercise one command section driving two independent engines and two sections driving separate throttles; refuse conflicting input bindings and cross-Controller writes. Repeat equivalent commands under human and AI operation, mixed complexity profiles and fully autonomous entities. A human-operated System must still report factual faults to an AI Controller; addressed messages must survive handover, and important AI-to-AI/entity-wide messages must reach the shared Viewscreen without leaking private records. Save/replay must preserve settings, requests, reporter latches and Controller continuation without duplicate effects. See the [refactoring acceptance plan](controller-refactor-plan.md).

The following walkthroughs test design coherence. They are hypothetical acceptance situations, not completed playtests or authored starter plots. Use mixed Console complexity profiles, then repeat with stronger delegation and the two-player starter bundle. Additional larger-crew content needs its own authored roster and workload validation.

### 15.1 Rescue under threat

1. Comms receives a distress claim and partial needs; Science investigates supported location/hazard evidence. Captain coordinates an initial goal without a private truth display.
2. Helm chooses an approach. Engineering identifies available stock/power; Tactical commits a suitable craft or prepares a displacement request. Negotiation and planning alone reserve no entire future supply chain.
3. Science establishes the displacement destination, coordinates positioning/resources and lowers the obstructing shield sector if displacement is chosen. Preparation can overlap a jump request; navigator use cannot.
4. An away tree performs routine work, presents a decision to Comms when warranted and applies the authored default after timeout. During link loss, execution continues without exposing host-only outcomes to the crew.
5. A threat creates choices about protection, recovery and departure. Science releases passage or uses a supported abort; it does not automatically pre-empt an active extraction. Tactical's ordinary craft commitments and physical personnel recovery still apply.
6. Delivered people, abandoned supplies, pending questions, damage and accepted obligations persist. Debrief explains only outcomes the crew has grounds to know.

**Accept when:** each seat can identify a consequential choice and a useful dependency; no routine approval ceremony is necessary; simple Science can delegate passage/protection without losing investigation agency; simultaneous rescue/defence does not require one expert to operate every station. Workload conclusions require observation and player reports.

### 15.2 Engagement and damage recovery

1. Basic returns appear on relevant radars. Science interprets evidence; Comms assesses traffic and possible psychic interference; Captain coordinates through crew conversation and the published Objectives list.
2. Tactical chooses targets/defence priorities independently of illumination. Helm establishes geometry while Engineering prepares resource margins. Weapon families exhibit different supported constraints.
3. Incoming damage follows a deck/compartment path, applies eligible component conditions and can interrupt a local service without destroying the ship. Operators report symptoms; Repair's list reflects delivered evidence.
4. Supply cuts affect allocated flow while local batteries continue applicable work. Thermal protection stops/resumes correctly. Engineering restores access/resources and commits teams/parts under the selected stock-allocation rule.
5. The crew can disable, negotiate, disengage, recover or abandon as supported. A disabled opponent is not forced to explode for the scenario to acknowledge its state.

**Accept when:** resource and damage feedback explains changed capability; AI has no privileged diagnosis/targets; repaired components remain disabled until structural recovery permits operation; useful human participation survives supported ship-loss paths.

### 15.3 Two-player operation and quiet work

Use the new Courier's movement/weapons and mission/support bundles with competent ordinary AI. Run investigation, aid, departure and an interrupted task. Neither player must configure multiple per-System depth settings or manually operate all Cruiser duties. Delegation must not remove consequential interpretation, resource or route choices. Quiet periods may contain planning and listening without filler failures.

### 15.4 Integrity and edge cases

| Case                                  | Required result                                                                                                |
| ------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| Competing passage requests            | One active operation; preparation can overlap; new urgency does not silently abort work.                       |
| Away prompt timeout/link loss         | Same tree advances through its authored default; later reports preserve uncertainty and timing.                |
| Scarce stock and competing work       | First eligible authorised request obtains available stock; committed stages/batches cannot be stolen silently. |
| Blocked delivery/receiving damage     | Quantity and location conserved; receiving claim does not create capacity or force arrival.                    |
| Full energy store                     | No unsupported intake or compulsory overflow waste; actual passive losses and heat still apply.                |
| Interrupted fabrication/recipe change | Follow job/recipe rules; no duplicate outputs, refunded consumption or false completed work.                   |
| Stale contact or lost illumination    | No hidden manoeuvre following, true-geometry reveal or universal firing permission gate.                       |
| Handover/reconnect/closed Console     | Preserve allowed knowledge, view context, work and reports; keep personal accessibility separate.              |
| Repair completion after a newer fault | Update the reported work without erasing a later independent condition.                                        |
| Abandonment/capture                   | Preserve identity and actual condition; obey human initiation and whole-ship handover boundaries.              |
| Resume during commitment/decision     | No reroll, repeated outcome, duplicated stock/personnel or restarted timeout.                                  |
| Routine travel compression            | Consistent elapsed time, arrivals and obligations; no unearned repair or silent protected choice.              |
| Director generation failure           | Bounded deterministic search terminates while current work and earned breathing room remain intact.            |
| Browser-only play                     | All essential information and actions remain accessible without optional hardware or native installation.      |

Pair instrumentation with observation and player reports across complete combat, non-combat and recovery situations, with and without a GM. Record meaningful alternatives, reasoning, communication, waiting, overload, handoffs and causes. Counts and win rates alone cannot prove enjoyment. Numerical performance budgets and workload thresholds require prototypes and measurements; this GDD does not invent tested values.

### 15.5 Testing policy

Use the three categories defined in the [testing policy](testing.md). **Smoke runs** are manually invoked playthrough/device/performance captures. **Verification** checks that authored ships, trees, equipment and packs are coherent and runnable without asserting their current tuning values. **System integration tests** load minimal synthetic test-only data through the actual loader, admission and execution mechanisms and assert the system's behavioral contract.

For example, verify that the Cruiser's declared power grid has valid providers, links, stores and baseline readiness; do not freeze its speed at 30 m/s. Test engine acceleration, resource consumption and refusal behavior using controlled synthetic parameters independent of the production Cruiser. Do not import a mandatory per-function/per-edit testing programme, production-content goldens or every historical smoke/performance threshold. The walkthroughs above define useful acceptance situations, not compulsory full journeys on every code change.

## 16. Authority, traceability and remaining specification work

### 16.1 Source precedence

1. User decisions in the V3 consolidation conversation, including fresh content, 45–90 minute missions, Science-owned passage, Comms-facing away trees/defaults, coolant recycling and first-eligible main-hold allocation.
2. The final [Settled Dark agreement](settled-dark-direction.md), which supersedes the earlier suggestion of a period centuries before the conquest.
3. Accepted [216-system review](../references/phoenix-v2/docs/gameplay-redesign/system-review.md), [architecture decisions](../references/phoenix-v2/docs/gameplay-redesign/architecture/README.md), adopted design refinements and revised future increment contracts. A later human selection supersedes an earlier alternative. V2 implementation particulars and AI-origin elaboration are not independently elevated into V3 product requirements.
4. The [old federated GDD](../references/phoenix-v2/docs/gdd/index.md), where compatible and not superseded.

The [source guide](source-guide.md) and [snapshot manifest](../references/source-manifest.json) identify original locations, hashes and collection boundaries. The [shared world](../references/settled-dark/shared-world/world.md), [chronology](../references/settled-dark/shared-world/chronology/eras.md) and [original setting](../references/settled-dark/original-setting/Setting.md) provide lore context. Later-period scenario inventions require deliberate adaptation.

### 16.2 Reconciled conflicts

| Historical material                                                 | V3 rule                                                                                                 |
| ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| Thin Margin setting and old ships/scenarios                         | New Settled Dark content; functional Courier/Cruiser starter scope only.                                |
| Conclave era several centuries before conquest                      | Immediately before the conquest; conquest itself outside the opening frame.                             |
| Preserve V2 network/runtime architecture                            | Preserve multiplayer/control/knowledge behavior on the newly selected C++/Flecs/raylib stack.           |
| V2 increments and acceptance claims                                 | Design evidence, not V3 delivery order or implementation status.                                        |
| Navigation treated as inherited Helm/auxiliary mapping              | Science controls Guild passage and coordinates preparation.                                             |
| Specialist-directed away work interpreted as repeated remote orders | Authored away state trees; occasional Comms decisions; authored timeout defaults even during link loss. |
| Coolant recycling marked provisional in the ledger                  | Optional fitted lossy recycling is included, confirmed during consolidation.                            |
| Generic proposed main-store scarcity priorities                     | First eligible authorised material request wins; Engineering can hold stock/cancel waiting work.        |
| Universal main-battery exhaustion/System percentage damage          | Local reserves, actual flow/thermal limits, compartment HP and component conditions.                    |
| One exclusive wide/focused scanner mode                             | Independent broad and focused Systems.                                                                  |
| Routine between-episode reset                                       | Preserve relevant stocks/conditions until supported servicing changes them.                             |

### 16.3 What remains to specify later

Gameplay direction is established here. Exact equipment values, reactor/weapon/thermal curves, work rates, team counts, check modifiers, casualty tables, spatial thresholds and workload calibration remain authored tuning and validation work. Names, district maps, detailed hull layouts and mission plots remain light content scope. Their values must obey the rules above rather than reopen them silently.

The [selected technical stack](technical-stack.md) fixes C++, Flecs, raylib, RmlUi, Dear ImGui docking and Emscripten. Further technical design selects dependency versions/build configuration, storage and transport, concrete APIs/schema/versioning, solver details and deterministic scheduling implementation. Preserve authoritative state, bounded knowledge, single control ownership, conservation, compatible continuation and the visible authoring behavior. The accepted shared mechanism and state-tree semantics remain contracts even when implementation changes. No V2 compatibility or code reuse is assumed. The [testing policy](testing.md) governs verification, synthetic behavioral tests and manually invoked smoke evidence.

If later content or implementation exposes a genuinely new product choice, record it as a proposed revision with its affected rules. Do not silently fill it by copying an abandoned source alternative. The reference snapshots remain unchanged.

## Appendix A. Catalogue coverage

Every historical catalogue ID appears once in the tables below. IDs are traceability handles, not separate modules or promises of initial-release scope. **Included** means the selected behavior is consolidated; **Boundary** records an exclusion/limit rather than adding the historical candidate. Original titles are retained for comparison, including superseded terminology. The V3 treatment and linked main chapter govern interpretation.

### Game systems — G001–G114

| ID   | Historical catalogue title                                          | V3 treatment                                                                                                                         | Chapter                                       |
| ---- | ------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------- |
| G001 | Station ownership and authored work bundles                         | Included: Author coherent station bundles per hull; players select seats rather than construct ownership layouts.                    | [3](#3-crew-stations-and-participation)       |
| G002 | Per-Station complexity and ratings                                  | Revised: Choose Console complexity profiles in lobby/settings; profiles allocate Controllers, without individual toggles.            | [3](#3-crew-stations-and-participation)       |
| G003 | Whole-System AI assistants                                          | Revised: Console-owned AI Controllers write owned command sections; physical Systems consume bindings and report facts.              | [4](#4-authority-knowledge-and-assistance)    |
| G004 | Vacant, disconnected and AFK Backfill                               | Revised: Ordinary Controller AI covers vacancy, disconnection and explicit Away; no automatic AFK detector is implied.               | [3](#3-crew-stations-and-participation)       |
| G005 | Human-seeking consoles and visiting tabs                            | Included: Human-seeking consoles offer eligible work without taking another human's authority.                                       | [3](#3-crew-stations-and-participation)       |
| G006 | AI stance and standing-order selection                              | Included: Supported orders and stances guide ordinary AI; explicit orders take precedence within limits.                             | [4](#4-authority-knowledge-and-assistance)    |
| G007 | System authority and shared physical rules                          | Included: Human and AI commands share authority checks and physical rules.                                                           | [4](#4-authority-knowledge-and-assistance)    |
| G008 | Ongoing and event-driven activity budget                            | Included: Allow meaningful ongoing work, preparation and quiet periods without filler chores.                                        | [1](#1-experience-and-boundaries)             |
| G009 | Reserve, grace and recoverable overload                             | Included: Actual reserves and readiness buy response time; no universal extra grace timer.                                           | [9](#9-energy-heat-and-material-supply)       |
| G010 | Continuous listening and deliberate coordination                    | Included: AI uses addressed text reports and urgency cues, without AI voices.                                                        | [7](#7-communications-and-psychic-warfare)    |
| G011 | Local expertise and distributed decision rights                     | Included: Specialists retain decisions; Captain coordinates goals without duplicate machinery controls.                              | [3](#3-crew-stations-and-participation)       |
| G012 | Spectating and post-loss participation                              | Included: Support useful continued participation after loss, with optional spectating.                                               | [3](#3-crew-stations-and-participation)       |
| G013 | Ready, launch, join and handover journey                            | Included: Provide straightforward join, claim, readiness, handover and reconnection journeys.                                        | [3](#3-crew-stations-and-participation)       |
| G014 | Personal accessibility and input assistance                         | Included: Keep personal accessibility separate from gameplay depth and shared Console state.                                         | [14](#14-fitting-authoring-and-presentation)  |
| G015 | Tutorials, manuals and rehearsal                                    | Included: Offer optional practice, shared exercises and manuals without tutorial entry gates.                                        | [3](#3-crew-stations-and-participation)       |
| G016 | Contact discovery and radar projection                              | Included: Relevant operational radars show basic returns; deeper findings require evidence.                                          | [6](#6-sensing-investigation-and-evidence)    |
| G017 | Detection, identification, classification and assessment            | Included: Automate broad assessment; allow deeper optional subject/question investigation.                                           | [6](#6-sensing-investigation-and-evidence)    |
| G018 | Per-observer evidence and confidence                                | Included: Track confidence per finding with readable summaries and provenance.                                                       | [6](#6-sensing-investigation-and-evidence)    |
| G019 | Contact persistence, ageing and prediction                          | Included: Predict from last observed motion with visible ageing and growing uncertainty.                                             | [6](#6-sensing-investigation-and-evidence)    |
| G020 | Wide, focused and intrusive scan trade-offs                         | Included: Broad and focused scanners operate independently with separate power demand.                                               | [6](#6-sensing-investigation-and-evidence)    |
| G021 | Emissions, signatures and concealment                               | Included: Operating machinery and emissions create readable exposure trade-offs.                                                     | [6](#6-sensing-investigation-and-evidence)    |
| G022 | Information sharing, provenance and Comms transfer                  | Included: Share selected scoped feeds; support human media and contextual generic requests.                                          | [7](#7-communications-and-psychic-warfare)    |
| G023 | Knowledge libraries and Intelligence correlation                    | Included: Attach routine references automatically; discover and explicitly attach unusual records.                                   | [6](#6-sensing-investigation-and-evidence)    |
| G024 | Interception, decryption and signal deception                       | Included: Use shared traffic, encrypted/public channels and limited interception/decoding.                                           | [7](#7-communications-and-psychic-warfare)    |
| G025 | Shared Viewscreen, alerts and attention hierarchy                   | Included: Crew discovers danger through normal tools; Captain sets alert without a private map.                                      | [3](#3-crew-stations-and-participation)       |
| G026 | Mission logs and recap                                              | Included: Retain automatic factual history, not compulsory officer prose or invented reasoning.                                      | [13](#13-scenarios-modes-and-continuity)      |
| G027 | Spatial geometry, movement and collision                            | Included: Capital motion is planar with limited collision avoidance; small craft move in 3D.                                         | [5](#5-flight-passage-and-operating-geometry) |
| G028 | Asteroid population and destructible terrain                        | Included: Meaningful selective terrain clearance persists long enough to change options.                                             | [12](#12-a-world-that-continues-working)      |
| G029 | Region hazards, interference and fog                                | Included: Hazards can offer contextual opportunities alongside coherent costs.                                                       | [12](#12-a-world-that-continues-working)      |
| G030 | Moving fields, falloff and physical emissions                       | Included: Fields evolve through authored properties and selected causal interventions.                                               | [12](#12-a-world-that-continues-working)      |
| G031 | Physical bodies, on-rails orbits and reference frames               | Included: Reference bodies remain static; orbital motion/windows are outside the baseline.                                           | [12](#12-a-world-that-continues-working)      |
| G032 | Relative motion and constrained holds                               | Included: Match observed speed without automatically holding position, aspect or range.                                              | [5](#5-flight-passage-and-operating-geometry) |
| G033 | Civilian traffic, routes and orders                                 | Included: Bounded civilian traffic pursues real relevant work and responds to circumstances.                                         | [12](#12-a-world-that-continues-working)      |
| G034 | Traffic priority, congestion, panic and emergency authority         | Included: Local services handle routine traffic; crews intervene in consequential exceptions.                                        | [12](#12-a-world-that-continues-working)      |
| G035 | Infrastructure condition, capacity and thresholds                   | Included: Facilities use damaged machinery and service thresholds rather than generic facility penalties.                            | [12](#12-a-world-that-continues-working)      |
| G036 | Infrastructure dependency and service networks                      | Included: Actual service dependencies propagate depletion, failure and recovery.                                                     | [12](#12-a-world-that-continues-working)      |
| G037 | Workforce, strike and disposition                                   | Included: Facility workforce reuses aggregate typed teams, not individually simulated workers.                                       | [11](#11-personnel-craft-and-away-missions)   |
| G038 | Actor needs, stocks and event-driven economy                        | Included: Use real stocks and contextual exchanges without a general market-price simulation.                                        | [12](#12-a-world-that-continues-working)      |
| G039 | NPC doctrine and autonomous reactions                               | Included: Doctrine and bounded variation guide ordinary AI without hidden stat advantages.                                           | [12](#12-a-world-that-continues-working)      |
| G040 | Factions, standing and reputation                                   | Included: Faction relationships coexist with actor trust based on known interactions.                                                | [12](#12-a-world-that-continues-working)      |
| G041 | Diplomacy, dialogue and commitments                                 | Included: Formal commitments are content-specific; generic requests do not universally create contracts.                             | [7](#7-communications-and-psychic-warfare)    |
| G042 | Surrender, compliance and coercion                                  | Included: Surrender changes behavior; capture has separate whole-ship handover requirements.                                         | [10](#10-damage-restoration-and-loss)         |
| G043 | Allied ship orders and support                                      | Included: Request allied support through Comms without central fleet actuator control.                                               | [12](#12-a-world-that-continues-working)      |
| G044 | Pursuit and inter-system pressure                                   | Included: Pursuit follows objectives, knowledge and capability rather than one universal leash.                                      | [12](#12-a-world-that-continues-working)      |
| G045 | Anomalies, black holes and wormholes                                | Included: Selected anomalies can alter usable routes as well as provide investigation/local effects.                                 | [12](#12-a-world-that-continues-working)      |
| G046 | Salvage, mining and discovery sites                                 | Included: Recover actual surviving cargo, equipment or materials through supported work.                                             | [9](#9-energy-heat-and-material-supply)       |
| G047 | Coarse Orbital Mode and planetary points of interest                | Included: Retain coarse wrapping planetary-map capability without actual orbital motion or on-foot play.                             | [12](#12-a-world-that-continues-working)      |
| G048 | Target acquisition, Combat Lock and Science designation             | Included: Tactical chooses primary and supported separate targets; Science designation is advisory.                                  | [8](#8-combat-and-protection)                 |
| G049 | Arcs, range, lead and exposure                                      | Included: Weapon-dependent range, tracking and geometry change effectiveness within the envelope.                                    | [8](#8-combat-and-protection)                 |
| G050 | Shield frequency, penetration and counterplay                       | Included: Optional shield probing finds configuration-specific response; avoid compulsory frequency matching.                        | [8](#8-combat-and-protection)                 |
| G051 | Incoming salvo, interception and defence saturation                 | Included: Finite interception, coverage and support create genuine defence saturation decisions.                                     | [8](#8-combat-and-protection)                 |
| G052 | Broadside / close-pass combat geometry                              | Included: Equipment supports distinct combat geometry; broadside combat is not universal.                                            | [8](#8-combat-and-protection)                 |
| G053 | Layered engagement ranges and transitions                           | Included: Layer ranges and escape/interdiction counterplay without imposing one engagement sequence.                                 | [8](#8-combat-and-protection)                 |
| G054 | Finite ammunition, launch commitment and fabrication                | Included: Ammunition and fabrication consume finite stocks, power and production time.                                               | [8](#8-combat-and-protection)                 |
| G055 | Power / battery / coolant / heat economy                            | Included: Use local energy, heat and consumable coolant, including optional lossy recycling.                                         | [9](#9-energy-heat-and-material-supply)       |
| G056 | Combat load / flux / local energy endurance                         | Included: Do not introduce a separate abstract combat-load or flux resource.                                                         | [9](#9-energy-heat-and-material-supply)       |
| G057 | Granular damage and capability loss                                 | Included: Component conditions change capabilities; compartments own structural HP.                                                  | [10](#10-damage-restoration-and-loss)         |
| G058 | Diagnosis, triage, team travel and restoration                      | Included: Diagnosis and typed-part restoration require supported evidence and actual teams/resources.                                | [10](#10-damage-restoration-and-loss)         |
| G059 | Fault propagation, isolation and containment                        | Included: Selected conditions worsen or spread causally; not every fault escalates.                                                  | [10](#10-damage-restoration-and-loss)         |
| G060 | Preventive maintenance and degradation                              | Included: Stress causes wear; healthy normal operation has no inevitable maintenance timer.                                          | [10](#10-damage-restoration-and-loss)         |
| G061 | Protected external operations under threat                          | Included: Withdrawal preserves physical personnel/equipment commitments; recovery requires actual work.                              | [11](#11-personnel-craft-and-away-missions)   |
| G062 | Asymmetric hulls, factions and doctrines                            | Included: Asymmetric fits and doctrine change decisions under common physical/knowledge rules.                                       | [8](#8-combat-and-protection)                 |
| G063 | Multi-bridge cooperation and competition                            | Included: Multi-bridge cooperation/competition preserves local authority and complementary work.                                     | [12](#12-a-world-that-continues-working)      |
| G064 | Disabled ships, rescue and loss continuation                        | Included: Distinguish recoverable disablement, abandonment, restoration and catastrophic loss.                                       | [10](#10-damage-restoration-and-loss)         |
| G065 | Resource capacities, transfer and replenishment                     | Included: Main/local stocks replenish through timed deliveries and actual compatible capacity.                                       | [9](#9-energy-heat-and-material-supply)       |
| G066 | Cargo lots, trade and hazardous goods                               | Included: Aggregate routine supplies; retain consequential consignment properties where useful.                                      | [9](#9-energy-heat-and-material-supply)       |
| G067 | Docked repair, rearm and service permission                         | Included: Ports replenish authorised targets; required trades are initiated from editable orders.                                    | [9](#9-energy-heat-and-material-supply)       |
| G068 | Personnel traits, duty teams and scarce expertise                   | Included: Named officers add expertise; typed teams remain functional without named leaders.                                         | [11](#11-personnel-craft-and-away-missions)   |
| G069 | Away Missions, extraction and support                               | Included: Away state trees use known-factor exact check odds, support and physical recovery.                                         | [11](#11-personnel-craft-and-away-missions)   |
| G070 | Boarding and internal defence                                       | Included: Whole teams use authored compartment routes for work, boarding and defence.                                                | [11](#11-personnel-craft-and-away-missions)   |
| G071 | Casualties, Medical and persistent recovery                         | Included: Explicitly lethal circumstances can permanently lose officers without excluding human players.                             | [11](#11-personnel-craft-and-away-missions)   |
| G072 | Sidegrade loadouts and starbase refit                               | Included: Allow limited compatible field changes; major refits require appropriate facilities.                                       | [14](#14-fitting-authoring-and-presentation)  |
| G073 | Typed mounts, compatibility and partitioned fit budgets             | Included: Use chiefly narrow physical mount compatibility and operating constraints, not abstract fitting currency.                  | [14](#14-fitting-authoring-and-presentation)  |
| G074 | Reactor profiles and sustainability forecasts                       | Included: Offer manageable reactor sidegrades and forecasts with stated assumptions.                                                 | [14](#14-fitting-authoring-and-presentation)  |
| G075 | Hull geometry, mass, layout and logistics topology                  | Included: Authors define hull topology; ordinary player fitting does not rearrange rooms or mounts.                                  | [14](#14-fitting-authoring-and-presentation)  |
| G076 | Crew skill, AI behaviour and officers as build choices              | Included: Officer assignments add choices without progression-gating competent basic assistance.                                     | [11](#11-personnel-craft-and-away-missions)   |
| G077 | Loot, upgrades and roguelite unlocks                                | Included: Mechanical rewards persist within runs/campaigns; fresh games require no account unlocks.                                  | [13](#13-scenarios-modes-and-continuity)      |
| G078 | Ship identity, naming and attachment                                | Included: Ship identity and factual history persist without an earned hull-trait modifier layer.                                     | [13](#13-scenarios-modes-and-continuity)      |
| G079 | Objectives, recipient scopes and AI directives                      | Included: Outcome-first objectives guide methods; optional procedures help ordinary AI.                                              | [13](#13-scenarios-modes-and-continuity)      |
| G080 | State triggers, thresholds, deadlines and branching outcomes        | Included: Intermediate failures usually change play; some authored terminal conditions can end it.                                   | [13](#13-scenarios-modes-and-continuity)      |
| G081 | Scarcity, permissions and irreversible allocation                   | Included: Actual authorised actions commit significant allocations; protected choices retain explicit authority.                     | [13](#13-scenarios-modes-and-continuity)      |
| G082 | Waves and reinforcement escalation                                  | Included: Reinforcements usually have caused dispatch/travel, with appropriate authored arrival exceptions.                          | [12](#12-a-world-that-continues-working)      |
| G083 | Coupled causal chains / emergent problems                           | Included: Reusable causal rules combine through actual prerequisites rather than only scripted chains.                               | [13](#13-scenarios-modes-and-continuity)      |
| G084 | Procedural situations / shared generator and Director               | Included: Bounded Director pacing changes unannounced work while preserving established consequences.                                | [13](#13-scenarios-modes-and-continuity)      |
| G085 | Authored reusable situation libraries and repetition control        | Included: Downweight repeated problem/solution combinations without suppressing causal outcomes.                                     | [13](#13-scenarios-modes-and-continuity)      |
| G086 | Patrol / persistent multi-system run                                | Included: Patrol normally assigns one main priority, with optional work and method freedom.                                          | [13](#13-scenarios-modes-and-continuity)      |
| G087 | War, allies, coarse fronts and decisive finale                      | Included: War has an authored finale boundary shaped by earlier operational consequences.                                            | [13](#13-scenarios-modes-and-continuity)      |
| G088 | Sandbox / player-chosen sector work                                 | Included: Sandbox permits offers and self-directed projects without compulsory job acceptance.                                       | [13](#13-scenarios-modes-and-continuity)      |
| G089 | Territory conquest and colony strategy                              | Included: Territorial change follows bridge operations; exclude a separate colony-budget management loop.                            | [13](#13-scenarios-modes-and-continuity)      |
| G090 | Campaign facts, branching persistence and recap                     | Included: Relevant stocks, conditions, obligations and assets persist until supported work changes them.                             | [13](#13-scenarios-modes-and-continuity)      |
| G091 | Save, checkpoint and compatible resume                              | Included: Compatible save/resume preserves work and outcomes without duplication or rerolls.                                         | [13](#13-scenarios-modes-and-continuity)      |
| G092 | Outcomes, debrief and repeat-session learning                       | Included: Debrief reflects crew knowledge; unrestricted diagnostics belong to author/test tools.                                     | [13](#13-scenarios-modes-and-continuity)      |
| G093 | Workload instrumentation and playtest scenarios                     | Included: Pair instrumentation with observation/player reports; activity counts do not prove engagement.                             | [15](#15-validation-scenarios-and-acceptance) |
| G094 | GM observing, puppeting and directing                               | Included: GM facilitation is optional; Director pacing continues unless paused or overridden.                                        | [13](#13-scenarios-modes-and-continuity)      |
| G095 | GM attention, history, undo and checkpoint recovery                 | Included: Provide attributed GM history and supported recovery without promising universal rewind.                                   | [13](#13-scenarios-modes-and-continuity)      |
| G096 | Information control and in-sim event cues                           | Included: Misleading reports remain distinguishable from physical truth and have investigable clues.                                 | [13](#13-scenarios-modes-and-continuity)      |
| G097 | Campaign continuity / advisory pacing tools                         | Included: Campaign/pacing advice supports GM decisions without automatically executing them.                                         | [13](#13-scenarios-modes-and-continuity)      |
| G098 | Workshop, authoring, disposable Test and packs                      | Included: Workshop, packs, history and disposable Test have distinct Author/Test/Live boundaries.                                    | [14](#14-fitting-authoring-and-presentation)  |
| G099 | Models, views, audio and localisation                               | Included: Readable presentation, non-voice cues, localisation and accessible alternatives support operation.                         | [14](#14-fitting-authoring-and-presentation)  |
| G100 | External peripherals and venue show control                         | Included: Peripherals and venue effects are optional adapters; browser play remains complete.                                        | [14](#14-fitting-authoring-and-presentation)  |
| G101 | Difficulty, assistance and challenge tuning                         | Included: Shared challenge is explicit and separate from personal depth and human crew count.                                        | [1](#1-experience-and-boundaries)             |
| G102 | Explicit research boundaries                                        | Boundary: external operations are console-managed; directly playable on-foot mode is excluded.                                       | [1](#1-experience-and-boundaries)             |
| G103 | Transcription, human-error puzzles and multi-officer rituals        | Boundary: operational interpretation replaces routine transcription, puzzle codes and approval rituals.                              | [1](#1-experience-and-boundaries)             |
| G104 | Fitting simulator, blueprint sharing and theorycrafting             | Included: Fitting favourites/rehearsal preserve explicit station claimant choices against later whole-ship imports.                  | [14](#14-fitting-authoring-and-presentation)  |
| G105 | Search-to-engagement chain and guidance continuity                  | Included: Equipment-specific search/guidance supports engagement without universal lock or illumination permission.                  | [6](#6-sensing-investigation-and-evidence)    |
| G106 | Bearing triangulation / multisensor hypothesis testing              | Included: Selective hypotheses, wireframes and uncertainty bounds support meaningful sensor ambiguity.                               | [6](#6-sensing-investigation-and-evidence)    |
| G107 | Preparation, commitment and recovery windows                        | Included: Selected equipment supports preparation/release stages without a universal ready/fire cycle.                               | [8](#8-combat-and-protection)                 |
| G108 | Located resources, delivery, interception and service dependencies  | Included: Inter-location deliveries in the active sector use physical vessels and actual arrival.                                    | [9](#9-energy-heat-and-material-supply)       |
| G109 | Reports, requests and verified versus claimed state                 | Included: Distinguish claims from verified state with automatically retained source, age and confidence.                             | [7](#7-communications-and-psychic-warfare)    |
| G110 | Manoeuvre and operation effects on readiness / faults               | Included: Movement constraints apply to selected major operations, not routine healing or maintenance.                               | [10](#10-damage-restoration-and-loss)         |
| G111 | Throughput, queues, buffers and shifting bottlenecks                | Included: Real throughput, queues and buffers create bottlenecks with useful automation.                                             | [9](#9-energy-heat-and-material-supply)       |
| G112 | Autonomous outside actors and bounded competing crises              | Included: Outside actors perform actual work that can change needs without automatically granting objective credit.                  | [12](#12-a-world-that-continues-working)      |
| G113 | Telegraphing intent and preventive counterplay                      | Included: Observable preparation supports inference without exposing exact hidden plans or timers.                                   | [6](#6-sensing-investigation-and-evidence)    |
| G114 | Persistent settings, explicit work ownership and temporary handoffs | Revised: Settings and physical work persist across handover under explicit Controller-section ownership and physical input bindings. | [4](#4-authority-knowledge-and-assistance)    |

### Ship systems — S001–S102

| ID   | Historical catalogue title                                      | V3 treatment                                                                                                                         | Chapter                                       |
| ---- | --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------- |
| S001 | Phasers / sustained beam banks                                  | Included: Sustain/stop specialist beams within actual geometry, reserve and thermal limits.                                          | [8](#8-combat-and-protection)                 |
| S002 | Blasters / unguided projectile banks                            | Included: Support ammunition-fed kinetics and equipment-appropriate energy bolts with shared projectile mechanisms.                  | [8](#8-combat-and-protection)                 |
| S003 | Guided torpedoes / missiles                                     | Included: Suitable ordnance accepts link-dependent post-launch instructions; preserve launched physical payload properties.          | [8](#8-combat-and-protection)                 |
| S004 | Magazine, tube loading and reload                               | Included: Authored launcher groups can share finite loading capacity and tube priorities.                                            | [8](#8-combat-and-protection)                 |
| S005 | Weapon tuning and beam frequency                                | Included: Equipment-supported weapon tuning interacts with measured shield response without universal matching chores.               | [8](#8-combat-and-protection)                 |
| S006 | Weapon-bank charge, heat and discharge                          | Included: Weapon reserves use local System batteries rather than another mandatory capacitor meter.                                  | [9](#9-energy-heat-and-material-supply)       |
| S007 | Broadside batteries                                             | Included: Broadside arcs and finite turret traversal make relative geometry consequential.                                           | [8](#8-combat-and-protection)                 |
| S008 | Point-defence cannons / PDCs                                    | Included: Suitable PDCs serve interception and short-range attack using competing finite capacity.                                   | [8](#8-combat-and-protection)                 |
| S009 | Long-range kinetic guns / railguns                              | Included: Selected heavy guns produce physical recoil with supported compensation.                                                   | [8](#8-combat-and-protection)                 |
| S010 | EMP / disruption payloads                                       | Included: Temporary disruption refreshes with stacking longer resistance and non-shortening reapplication.                           | [8](#8-combat-and-protection)                 |
| S011 | Explosive / area-effect warheads                                | Included: Area effects can harm physically exposed allies/civilians; policies do not grant immunity.                                 | [8](#8-combat-and-protection)                 |
| S012 | Penetrating / marking payloads                                  | Included: Delivered marking beacons provide limited communicated observations, not permanent perfect tracks.                         | [6](#6-sensing-investigation-and-evidence)    |
| S013 | Persistent mines and minelayers                                 | Included: Mine discrimination depends on fitted sensing and trigger policy, including indiscriminate variants.                       | [8](#8-combat-and-protection)                 |
| S014 | Ammunition fabrication                                          | Included: Ammunition and repairs can share selected material categories while retaining specialised inputs.                          | [9](#9-energy-heat-and-material-supply)       |
| S015 | Fire groups and independent bank targeting                      | Included: Prepared groups coexist with individual System overrides that group commands must respect.                                 | [8](#8-combat-and-protection)                 |
| S016 | Targeted subsystem attack                                       | Included: Aim toward known equipment compartments; actual paths/deck selection do not guarantee subsystem hits.                      | [10](#10-damage-restoration-and-loss)         |
| S017 | Fighter / attack-drone launch and recovery                      | Included: Manage bounded sorties and recovery; ordinary craft AI flies, without initial direct human piloting.                       | [11](#11-personnel-craft-and-away-missions)   |
| S018 | Electronic attack / Jam                                         | Included: Psychic interference uses fitted geometry/resources, observable effects and possible friendly interference.                | [7](#7-communications-and-psychic-warfare)    |
| S019 | Spoofing and decoy emitters                                     | Included: Physical spoofing/decoy entities have finite equipment, power, sensing and lifetime.                                       | [7](#7-communications-and-psychic-warfare)    |
| S020 | Electronic hardening / ECCM                                     | Included: Fitted psychic protection counters supported interference without universal immunity.                                      | [7](#7-communications-and-psychic-warfare)    |
| S021 | Hacking / remote access / mind-control devices                  | Included: Psychic access/disruption affects supported information/capabilities without hostile System takeover.                      | [7](#7-communications-and-psychic-warfare)    |
| S022 | Shield generators and directional arcs                          | Included: Directional shield generators consume actual reserve, flow and thermal capacity.                                           | [8](#8-combat-and-protection)                 |
| S023 | Shield focus / redistribution                                   | Included: Science chooses supported shield priorities and redistribution; Helm controls exposure.                                    | [8](#8-combat-and-protection)                 |
| S024 | Shield modulation / frequency retuning                          | Included: Every generator can retune while raised through a costly temporarily weaker transition.                                    | [8](#8-combat-and-protection)                 |
| S025 | Armour, hull sections and structural protection                 | Included: Compartment materials/HP govern structural protection and supported full restoration.                                      | [10](#10-damage-restoration-and-loss)         |
| S026 | Cloaking / concealment device                                   | Included: Emissions alter detection while cloak may remain active; old evidence is not erased.                                       | [6](#6-sensing-investigation-and-evidence)    |
| S027 | Signature and emission controls                                 | Included: Human low-emission profiles are advisory; no inhibition or repeated confirmation gate.                                     | [6](#6-sensing-investigation-and-evidence)    |
| S028 | Damage isolation / breakers / bypasses                          | Included: Operate fitted breakers/routes remotely; new bypasses require physical team work/materials.                                | [10](#10-damage-restoration-and-loss)         |
| S029 | Fire suppression, compartment seals and venting                 | Included: Coarse compartment boundaries govern applicable fire, containment, venting and team access.                                | [10](#10-damage-restoration-and-loss)         |
| S030 | Self-destruct interlocks                                        | Included: Distributed self-destruct requires human initiation with humans aboard and visible supported steps.                        | [10](#10-damage-restoration-and-loss)         |
| S031 | Reactor / power generation                                      | Included: Commanded reactor output has finite response, local heat/coolant and capacitor reserves.                                   | [9](#9-energy-heat-and-material-supply)       |
| S032 | Battery / energy reserve                                        | Included: Default storage is reactor buffer plus consumer-local batteries; extra storage is optional fit.                            | [9](#9-energy-heat-and-material-supply)       |
| S033 | Power distribution                                              | Included: Distribution adapts within assigned group budgets; Engineering retains allocation authority.                               | [9](#9-energy-heat-and-material-supply)       |
| S034 | Coolant allocation / thermal circulation                        | Included: Finite cooling consumes coolant for removed heat; optional fitted recycling has net loss.                                  | [9](#9-energy-heat-and-material-supply)       |
| S035 | Heat rejection / radiators                                      | Included: Passive heat rejection is authored; exclude a separate radiator deployment/output control loop.                            | [9](#9-energy-heat-and-material-supply)       |
| S036 | Capacitors / weapon energy buffers                              | Included: Reuse local energy buffers without another mandatory shot-energy store.                                                    | [9](#9-energy-heat-and-material-supply)       |
| S037 | Overpower / overcharge control                                  | Included: Owner-controlled fitted overdrive raises real costs/stress; normal thermal protection remains.                             | [9](#9-energy-heat-and-material-supply)       |
| S038 | Presets, load shedding and emergency recovery                   | Included: Energy scarcity limits work without generic restart locks; preserve explicit flow requests on cuts.                        | [9](#9-energy-heat-and-material-supply)       |
| S039 | Containment / resource conduits and buffers                     | Included: Coarse physical conduit segments, redundancy and feasible bypass work constrain actual supply.                             | [10](#10-damage-restoration-and-loss)         |
| S040 | Life support                                                    | Included: Compartment reserves support local survival margins rather than one universal ship countdown.                              | [10](#10-damage-restoration-and-loss)         |
| S041 | Main-drive propellant / fuel supply                             | Included: Finite interstellar fuel remains; manoeuvring propellant is abstracted while energy matters.                               | [5](#5-flight-passage-and-operating-geometry) |
| S042 | Repair materials and fabrication stock                          | Included: Cannibalisation sacrifices actual compatible donor capability through authorised work.                                     | [9](#9-energy-heat-and-material-supply)       |
| S043 | Internal logistics and delivery routes                          | Included: Automatic aggregate-batch delivery uses reachable routes and finite handling throughput.                                   | [9](#9-energy-heat-and-material-supply)       |
| S044 | Main engines / thrust and reverse                               | Included: Desired-speed assistance accelerates/brakes through actual engines and resources.                                          | [5](#5-flight-passage-and-operating-geometry) |
| S045 | Steering / yaw and attitude control                             | Included: Desired absolute heading controls turning; it does not automatically track a target.                                       | [5](#5-flight-passage-and-operating-geometry) |
| S046 | Lateral / vertical manoeuvring thrusters                        | Included: Fitted lateral thrust and a visible direct-control toggle preserve common physics.                                         | [5](#5-flight-passage-and-operating-geometry) |
| S047 | Boost / impulse burst                                           | Included: Sustain/release fitted boost; ending thrust does not erase momentum.                                                       | [5](#5-flight-passage-and-operating-geometry) |
| S048 | Relative-motion assist / match, hold, intercept                 | Included: Match Speed holds stale observed speed with feedback until reacquisition or cancellation.                                  | [5](#5-flight-passage-and-operating-geometry) |
| S049 | Docking / capture and mating apparatus                          | Included: Docking/mating and separation are physical; emergency release requires supported equipment.                                | [5](#5-flight-passage-and-operating-geometry) |
| S050 | Warp drive / inter-system transit                               | Included: Science coordinates discrete Guild interstellar passage with compressed routine transit.                                   | [5](#5-flight-passage-and-operating-geometry) |
| S051 | Jump drive / tactical displacement                              | Included: Specialist local jumps select spatial destinations without a universal receiving anchor.                                   | [5](#5-flight-passage-and-operating-geometry) |
| S052 | Navigation chart / waypoint planner                             | Included: Optional multi-leg routes show their current leg; simple travel needs one destination.                                     | [5](#5-flight-passage-and-operating-geometry) |
| S053 | Autopilot and formation / escort controller                     | Included: Optional formation slots use delegated individual movement AI and actual feasibility.                                      | [5](#5-flight-passage-and-operating-geometry) |
| S054 | Helm radar                                                      | Included: Helm radar exposes permitted movement-relevant returns, not universal detailed knowledge.                                  | [6](#6-sensing-investigation-and-evidence)    |
| S055 | Tactical radar / Combat Lock                                    | Included: Tactical radar supports independent targeting without a universal Combat Lock requirement.                                 | [6](#6-sensing-investigation-and-evidence)    |
| S056 | Sensor radar / Science Target                                   | Included: Science selects investigation subjects and supported questions, not guaranteed answers.                                    | [6](#6-sensing-investigation-and-evidence)    |
| S057 | Wide survey / search scanner                                    | Included: Independent broad survey performs routine evidence-limited assessment automatically.                                       | [6](#6-sensing-investigation-and-evidence)    |
| S058 | Focused scanner / structural scan                               | Included: Each fitted focused scanner runs one enquiry with a queue; multiple units can work concurrently.                           | [6](#6-sensing-investigation-and-evidence)    |
| S059 | Intrusive / deep scanner                                        | Included: Suitable intrusive scanning occupies the scanner and can expose its activity.                                              | [6](#6-sensing-investigation-and-evidence)    |
| S060 | Passive receivers / signal analysis                             | Included: Independent passive receivers can listen alongside active sensors without guaranteed stealth/decryption.                   | [6](#6-sensing-investigation-and-evidence)    |
| S061 | Probes and remote sensor network                                | Included: Stationary/mobile probes use actual equipment, routes, endurance and received reports.                                     | [6](#6-sensing-investigation-and-evidence)    |
| S062 | Database / Intelligence correlation tools                       | Included: Held-record retrieval has no extra processing budget; ordinary AI takes reading time for text.                             | [6](#6-sensing-investigation-and-evidence)    |
| S063 | Contact designation and information transfer                    | Included: Retained findings rejoin normal history over restored feeds without a separate catch-up System.                            | [6](#6-sensing-investigation-and-evidence)    |
| S064 | Environment / resource / anomaly scanner                        | Included: Environmental forecasts expose assumptions/uncertainty and cannot read hidden future causes.                               | [6](#6-sensing-investigation-and-evidence)    |
| S065 | Hail / Comms transceiver                                        | Included: Persistent concurrent conversation threads support urgency and selected audio/video focus.                                 | [7](#7-communications-and-psychic-warfare)    |
| S066 | Interception / frequency search receiver                        | Included: Finite interception coverage follows priorities; routine public chatter remains automatic.                                 | [7](#7-communications-and-psychic-warfare)    |
| S067 | Cipher / decoding suite                                         | Included: Decoding can require capability or keys; elapsed time does not guarantee plaintext.                                        | [7](#7-communications-and-psychic-warfare)    |
| S068 | Dossier / commitment interface                                  | Included: Dossiers show terms/evidence/status; Comms judges risk rather than automatic risk scoring.                                 | [7](#7-communications-and-psychic-warfare)    |
| S069 | Civilian / allied order terminal                                | Included: Fixed audiences include own fleet, each faction and all ships; individual requests remain.                                 | [7](#7-communications-and-psychic-warfare)    |
| S070 | Captain status / objectives dashboard                           | Revised: Objectives list and advisory priority selection when any Controller is AI-operated; no Captain reporting/display dashboard. | [3](#3-crew-stations-and-participation)       |
| S071 | Alert posture / Red Alert                                       | Included: A small selected alert ladder changes actual readiness/restraint, not free buffs.                                          | [3](#3-crew-stations-and-participation)       |
| S072 | Command / AI stance controller                                  | Included: Explicit supported orders take precedence over general stance within authority and physical limits.                        | [4](#4-authority-knowledge-and-assistance)    |
| S073 | Viewscreen and auxiliary display controls                       | Included: Deliberate Viewscreen publication follows the operator's current browsing until sharing ends.                              | [14](#14-fitting-authoring-and-presentation)  |
| S074 | Mission / officer log                                           | Included: Automatic factual history retains provenance without compulsory player-authored logs.                                      | [13](#13-scenarios-modes-and-continuity)      |
| S075 | Internal damage-control teams                                   | Included: One team repairs a component; parallel teams can work on different components.                                             | [10](#10-damage-restoration-and-loss)         |
| S076 | Diagnosis / tests / damage reports                              | Included: Observation-only diagnostics use actual evidence without deliberately disrupting the tested capability.                    | [10](#10-damage-restoration-and-loss)         |
| S077 | Repair execution / restoration                                  | Included: Use full remedy where feasible, otherwise an indefinite reduced-effectiveness patch without spread/expiry.                 | [10](#10-damage-restoration-and-loss)         |
| S078 | Proactive inspection / preventive maintenance                   | Included: Prevent stress wear through operating choices; repair known wear without routine pre-wear chores.                          | [10](#10-damage-restoration-and-loss)         |
| S079 | Duty Team roster and assignments                                | Included: Finite typed teams have stable authored competencies and physical commitments.                                             | [11](#11-personnel-craft-and-away-missions)   |
| S080 | Duty Officers / expertise assignments                           | Included: Officer assignment bonuses begin after actual coarse travel to compatible duties.                                          | [11](#11-personnel-craft-and-away-missions)   |
| S081 | Medical / recovery services                                     | Included: Science manages treatment priorities; minor/severe conditions constrain availability without routine deterioration.        | [11](#11-personnel-craft-and-away-missions)   |
| S082 | Internal defence / boarding support                             | Included: Whole-team boarding/defence objectives use actual access and supported encounters.                                         | [11](#11-personnel-craft-and-away-missions)   |
| S083 | Tractor beam / tow and stabilise                                | Included: Fitted force projectors can influence resisting/shielded vessels within real limits.                                       | [5](#5-flight-passage-and-operating-geometry) |
| S084 | Umbilical / capacity transfer                                   | Included: Compatible umbilicals transfer in parallel through finite throughput and authorised priorities.                            | [11](#11-personnel-craft-and-away-missions)   |
| S085 | External repair / field team                                    | Included: Delivered teams can repair independently using local supplies/tools, with actual later recovery.                           | [11](#11-personnel-craft-and-away-missions)   |
| S086 | Shuttle / carried utility craft                                 | Included: Craft and team commitments are separate; shuttles can leave only when tasks permit.                                        | [11](#11-personnel-craft-and-away-missions)   |
| S087 | Transporter / remote personnel deployment                       | Included: Science-owned displacement needs established destinations, lowered obstructing shields and shared navigator capacity.      | [5](#5-flight-passage-and-operating-geometry) |
| S088 | Away Mission Operations systems                                 | Included: Away state trees execute autonomously, surface Comms choices and use authored timeout defaults.                            | [11](#11-personnel-craft-and-away-missions)   |
| S089 | Cargo hold / bulk stores / hazardous containment                | Included: Compatible compartment stores preserve actual capacity, cargo condition and hazards.                                       | [9](#9-energy-heat-and-material-supply)       |
| S090 | Salvage / recovery equipment                                    | Included: Recover actual surviving components/materials rather than generating generic loot independently of state.                  | [9](#9-energy-heat-and-material-supply)       |
| S091 | Mining / harvesting tools                                       | Included: Optional general harvesting supports other work without compulsory mining progression.                                     | [9](#9-energy-heat-and-material-supply)       |
| S092 | Supply / logistics drones                                       | Included: Authorised logistics drones make actual deliveries within stocks, links, capacity and commitments.                         | [11](#11-personnel-craft-and-away-missions)   |
| S093 | Construction / fabrication / repair drones                      | Included: Suitable drones construct/deploy or repair supported equipment with actual compatible materials and work.                  | [11](#11-personnel-craft-and-away-missions)   |
| S094 | Remote shield / energy / repair support emitters                | Included: Optional remote energy/shield assistance remains; physical repair emitters are excluded.                                   | [8](#8-combat-and-protection)                 |
| S095 | Repair puzzles and reactivation workflows                       | Boundary: restoration has no transcription/repair puzzle or mandatory reactivation ritual layer.                                     | [10](#10-damage-restoration-and-loss)         |
| S096 | Fire-control radar / illuminator                                | Included: Optional finite fire-control/illumination hardware supports selected weapons, never all firing universally.                | [6](#6-sensing-investigation-and-evidence)    |
| S097 | Bearing-only receivers / ELINT / passive thermal search         | Included: Bearing/signature detections may lack range; localisation improves through legitimate observations.                        | [6](#6-sensing-investigation-and-evidence)    |
| S098 | Directional search array / reorienting reconnaissance suite     | Included: Reorientable directional search equipment trades coverage and attention under actual sensing limits.                       | [6](#6-sensing-investigation-and-evidence)    |
| S099 | Target-motion / firing-solution computer                        | Included: Integrate automated motion/firing estimates into relevant sensors/fire-control, not a new mandatory computer System.       | [6](#6-sensing-investigation-and-evidence)    |
| S100 | Escape-pod beacon, locator and recovery apparatus               | Included: Distress beacons make pods discoverable through actual signals; rescue remains physical work.                              | [11](#11-personnel-craft-and-away-missions)   |
| S101 | Remote-craft command / telemetry datalink                       | Included: Relays/datalinks deliver permitted messages and telemetry without bandwidth-capacity accounting.                           | [7](#7-communications-and-psychic-warfare)    |
| S102 | Compartment atmosphere / pressure control and emergency pumping | Included: Coarse pressure/atmosphere and survival conditions avoid detailed fluid equalisation; pumping is hazard-specific.          | [10](#10-damage-restoration-and-loss)         |

## Appendix B. Architecture decision reconciliation

All 76 reviewed architecture decisions are accounted for below. Behavior and authoring contracts carry forward; language/runtime/storage choices and V2 migration obligations do not. These are logical requirements, not a prescribed codebase layout or public API. The revised mission-planning contracts following AD76 are consolidated in chapter 13. The current away-tree timeout/default rule also comes from the explicit consolidation clarification, not an inference from the older summaries.

| Decision | Consolidated treatment                                                                                                                | Chapter                                      |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------- |
| AD01     | Revised: one effective operator per Controller, one owner per command section and one source per physical input.                      | [4](#4-authority-knowledge-and-assistance)   |
| AD02     | Revised: Controller AIs and GUI share permitted Console observations; entity board serves all AIs and shared Viewscreen.              | [4](#4-authority-knowledge-and-assistance)   |
| AD03     | Physical condition/repair parts and reusable mechanisms have explicit distinct bindings.                                              | [10](#10-damage-restoration-and-loss)        |
| AD04     | Shared services are explicit; turret capabilities consume compatible Controller channels without competing physical input writers.    | [4](#4-authority-knowledge-and-assistance)   |
| AD05     | Controller-owned command sections drive explicitly bound physical inputs; one output can serve multiple Systems.                      | [4](#4-authority-knowledge-and-assistance)   |
| AD06     | Resource and information links preserve their separate constraints and ownership.                                                     | [4](#4-authority-knowledge-and-assistance)   |
| AD07     | Authored scoped feeds update automatically without publishing all Console knowledge.                                                  | [6](#6-sensing-investigation-and-evidence)   |
| AD08     | Show partial capability/status and known conditions rather than a binary health label.                                                | [14](#14-fitting-authoring-and-presentation) |
| AD09     | Prototype Console screens by composing and binding supported reusable widgets.                                                        | [14](#14-fitting-authoring-and-presentation) |
| AD10     | Prototype rectangles and circle-cutout/corner presets, not arbitrary free-form packing.                                               | [14](#14-fitting-authoring-and-presentation) |
| AD11     | Shared responsive fallbacks preserve actions, order, labels, targets and bindings.                                                    | [14](#14-fitting-authoring-and-presentation) |
| AD12     | Console working context follows handover; personal accessibility remains the incoming player's.                                       | [3](#3-crew-stations-and-participation)      |
| AD13     | Workshop composes supported mechanisms; new primitives require implementation work.                                                   | [14](#14-fitting-authoring-and-presentation) |
| AD14     | Revised: Console Controller trees use board variables, owned command writes, addressed messages and private instance memory.          | [4](#4-authority-knowledge-and-assistance)   |
| AD15     | Inheritance can change parameters and named structure; validate the resolved definition.                                              | [4](#4-authority-knowledge-and-assistance)   |
| AD16     | Reference nodes call reusable trees within distinct instance/context boundaries.                                                      | [4](#4-authority-knowledge-and-assistance)   |
| AD17     | Callers can interrupt referenced decision trees without cancelling physical work by implication.                                      | [4](#4-authority-knowledge-and-assistance)   |
| AD18     | Deterministic task ordering resolves competing writes by later-write precedence.                                                      | [4](#4-authority-knowledge-and-assistance)   |
| AD19     | Later evaluation can read earlier internal writes without confusing intent with physical results.                                     | [4](#4-authority-knowledge-and-assistance)   |
| AD20     | Revised: always-active factual System reporters expose permitted faults regardless of human/AI Controller operation.                  | [10](#10-damage-restoration-and-loss)        |
| AD21     | Repair retains report-driven work records; completion does not reveal unrelated hidden state.                                         | [10](#10-damage-restoration-and-loss)        |
| AD22     | Finite activities share job lifecycle contracts with activity-specific execution rules.                                               | [4](#4-authority-knowledge-and-assistance)   |
| AD23     | Reserve for the next executable stage, not all future queued work.                                                                    | [4](#4-authority-knowledge-and-assistance)   |
| AD24     | Job workflows reuse tree authoring/evaluation with separate context and supported vocabulary.                                         | [4](#4-authority-knowledge-and-assistance)   |
| AD25     | Initial jobs are independent with reusable subtrees; no separate nested-job lifecycle.                                                | [4](#4-authority-knowledge-and-assistance)   |
| AD26     | Shared conditions/modifiers produce explicit supported capability effects.                                                            | [10](#10-damage-restoration-and-loss)        |
| AD27     | Condition progression composes supported rules, not arbitrary privileged author code.                                                 | [14](#14-fitting-authoring-and-presentation) |
| AD28     | Assemblies resolve explicit default service connections and permitted local overrides.                                                | [14](#14-fitting-authoring-and-presentation) |
| AD29     | Batch/continuous recipes share finite input/output/work accounting.                                                                   | [9](#9-energy-heat-and-material-supply)      |
| AD30     | Scenario progression uses authored visual state trees.                                                                                | [13](#13-scenarios-modes-and-continuity)     |
| AD31     | Retain sandboxed scenario-script integration behavior; V2's Rhai language is not a V3 stack decision.                                 | [13](#13-scenarios-modes-and-continuity)     |
| AD32     | Reusable situation instances retain independent runtime state and identity.                                                           | [13](#13-scenarios-modes-and-continuity)     |
| AD33     | Situations coordinate through declared contracts/events rather than private-memory reads.                                             | [13](#13-scenarios-modes-and-continuity)     |
| AD34     | Use shared provenance-preserving observation/report records across supported consumers.                                               | [4](#4-authority-knowledge-and-assistance)   |
| AD35     | Console-owned knowledge processing combines permitted evidence without omniscient inference or extra analysis timers.                 | [6](#6-sensing-investigation-and-evidence)   |
| AD36     | Reusable multi-System assemblies have distinct placement identities without extra command authority.                                  | [14](#14-fitting-authoring-and-presentation) |
| AD37     | Physical compartments and component/System/compartment template units remain explicit.                                                | [10](#10-damage-restoration-and-loss)        |
| AD38     | A System's equipment belongs to one compartment; service connections can cross boundaries.                                            | [10](#10-damage-restoration-and-loss)        |
| AD39     | Each deck has a 2D physical layout for damage, repair and compartment membership.                                                     | [10](#10-damage-restoration-and-loss)        |
| AD40     | Ordinary incoming damage follows its supported incoming planar line, not arbitrary subsystem teleportation.                           | [10](#10-damage-restoration-and-loss)        |
| AD41     | Separate penetration from deposited damage/effects without multiplying payload on traversal.                                          | [8](#8-combat-and-protection)                |
| AD42     | Material properties belong to compartments, not overlapping private component hull models.                                            | [10](#10-damage-restoration-and-loss)        |
| AD43     | Charge penetration resistance by distance through compartment material.                                                               | [10](#10-damage-restoration-and-loss)        |
| AD44     | Compartments own structural HP; component conditions arise from supported damage effects.                                             | [10](#10-damage-restoration-and-loss)        |
| AD45     | Select a weighted random deck independently of shot Z height.                                                                         | [10](#10-damage-restoration-and-loss)        |
| AD46     | Effective resistance progressively declines with compartment structural damage.                                                       | [10](#10-damage-restoration-and-loss)        |
| AD47     | Deposited damage and susceptibility govern eligible condition chance/severity.                                                        | [10](#10-damage-restoration-and-loss)        |
| AD48     | Select eligible component faults compartment-wide without precise internal component coordinates.                                     | [10](#10-damage-restoration-and-loss)        |
| AD49     | Zero compartment HP disables its contents through a distinct condition.                                                               | [10](#10-damage-restoration-and-loss)        |
| AD50     | Clear structural disablement at an authored partial recovery threshold; reapply only at zero.                                         | [10](#10-damage-restoration-and-loss)        |
| AD51     | Allow compatible component and structural repair concurrently without premature reactivation.                                         | [10](#10-damage-restoration-and-loss)        |
| AD52     | Revised: zero HP in every compartment destroys the ship; other supported catastrophes and evidence-based disablement remain distinct. | [10](#10-damage-restoration-and-loss)        |
| AD53     | Abandonment defaults to non-hostile standby while retaining feasible survival/distress work.                                          | [10](#10-damage-restoration-and-loss)        |
| AD54     | Humans initiate abandonment when aboard; ordinary AI performs authorised delegated steps.                                             | [10](#10-damage-restoration-and-loss)        |
| AD55     | Return the same abandoned ship to service through supported personnel transfer/restoration.                                           | [10](#10-damage-restoration-and-loss)        |
| AD56     | Supported capture transfers whole-ship control, not room-by-room System authority.                                                    | [10](#10-damage-restoration-and-loss)        |
| AD57     | Capture exposes already-readable retained Console records, not hidden/encrypted/private AI truth.                                     | [10](#10-damage-restoration-and-loss)        |
| AD58     | Teams and deliveries route over authored access/safety-aware compartment links.                                                       | [9](#9-energy-heat-and-material-supply)      |
| AD59     | Internal deliveries are located aggregate batches with conserved quantity/progress.                                                   | [9](#9-energy-heat-and-material-supply)      |
| AD60     | Dispatch reserves compatible destination space; queueing alone does not.                                                              | [9](#9-energy-heat-and-material-supply)      |
| AD61     | Routine logistics has throughput/concurrency limits without a separate porter team per batch.                                         | [9](#9-energy-heat-and-material-supply)      |
| AD62     | Required output blockage pauses/slows a recipe while preserving legitimate work and stock.                                            | [9](#9-energy-heat-and-material-supply)      |
| AD63     | Deliberate recipe changes discard unfinished work and retain compatible stock within capacity.                                        | [9](#9-energy-heat-and-material-supply)      |
| AD64     | Implementation defines physical processing phases; authors compose supported mechanisms and workflows.                                | [14](#14-fitting-authoring-and-presentation) |
| AD65     | Shared request/resolve/consume accounting remains; V3 main-hold stock uses first-eligible allocation.                                 | [9](#9-energy-heat-and-material-supply)      |
| AD66     | An indivisible stage secures its required resource set atomically; failed attempts leave no new partial claims.                       | [4](#4-authority-knowledge-and-assistance)   |
| AD67     | Reprioritisation affects waiting/subsequent work; active interruption requires an explicit supported action/cause.                    | [4](#4-authority-knowledge-and-assistance)   |
| AD68     | Shared typed events have scoped reactions and separate permitted observations.                                                        | [14](#14-fitting-authoring-and-presentation) |
| AD69     | Environment/equipment fields share suitable geometry/sampling with distinct supported effects.                                        | [12](#12-a-world-that-continues-working)     |
| AD70     | Applicable field effects act along signal paths, independently of endpoint exposure.                                                  | [12](#12-a-world-that-continues-working)     |
| AD71     | Substantial solid terrain can obstruct/attenuate applicable signals through authored bounds/properties.                               | [12](#12-a-world-that-continues-working)     |
| AD72     | Pending remote orders use authored expiry/supersession; accepted onboard work is not implicitly cancelled.                            | [7](#7-communications-and-psychic-warfare)   |
| AD73     | Delivery/acceptance receipts return automatically over viable links; absence remains uncertainty.                                     | [7](#7-communications-and-psychic-warfare)   |
| AD74     | Reject message-capacity, bandwidth-pool and traffic-budget simulation.                                                                | [7](#7-communications-and-psychic-warfare)   |
| AD75     | One actual boundary state serves access and containment; commanded closure is not proven sealing.                                     | [10](#10-damage-restoration-and-loss)        |
| AD76     | Automatic routes respect deliberate seals and request authorised access changes.                                                      | [9](#9-energy-heat-and-material-supply)      |

## Implementation decision: authoritative departure debriefs

The departure debrief freezes selected world facts, objective statuses and personnel disposition at the successful FTL boundary, regardless of crew discovery. This exception applies only to the departed crew’s report. In-mission Console information, AI policies and network projections retain their knowledge restrictions. Mission grading is authored separately from competition outcomes and does not freeze the world. See [mission objectives and debriefs](mission-objectives.md).

### Remaining Cruiser station delivery — 23 October 2026

The [Science, Comms and Captain contract](cruiser-science-comms-captain.md) records the implemented workload split, psychic relocation, fixed one-star Comms/Captain profiles and Captain Objectives list. Captain priority initially influences autonomous navigation only. This specific station agreement supersedes older generic workload examples.
