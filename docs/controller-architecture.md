# Console Controller architecture

Agreed architecture · 19 October 2026. This contract supersedes the earlier per-System command-AI boundary. The [GDD](GDD.md#4-authority-knowledge-and-assistance) defines the gameplay design; the [refactoring delivery record](controller-refactor-plan.md) separately records implementation, verification and acceptance limits.

## Purpose and scope

Humans and AI operate the same information and command interface. Physical Systems execute machinery; Console-owned Controllers make decisions. An entity with all Controllers operated by AI follows exactly the same execution path as a human-operated entity. Player-slot membership and network hosting do not select a different AI or physics implementation.

These are controllable-entity contracts. Ships are the current application, but sensor and other mechanism code must not require a ship classification, Helm station or human seat. Probes and space stations can later compose the same capabilities. Implementing either is outside this refactor.

## Runtime ownership

| Part                | Owns                                                                                                         | Does not own                                                          |
| ------------------- | ------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------- |
| Controllable entity | Physical state, entity blackboard and controllable layout                                                    | A separate NPC/player simulation                                      |
| Station             | Grouping of Consoles for participation and presentation                                                      | A merged knowledge board or actuator authority                        |
| Console             | Scoped observation board, Controller command sections, inboxes, complexity selection and operator assignment | Physical machinery truth                                              |
| Controller          | One command section, effective operator, addressed inbox and private AI continuation                         | Direct writes to physical components or another Controller's commands |
| Physical System     | Mechanism state, command-input bindings, observation outputs and factual reporting                           | Decision-making command AI                                            |
| GUI / Viewscreen    | Permitted presentation and human input adapters                                                              | Authoritative state or privileged world reads                         |

A single `ControllableLayout` component on the entity can describe Stations, Consoles, Controllers, supported complexity profiles and bindings. Mutable blackboards, operator assignments, requests, reporter latches and AI continuations remain separate from the definition. Console and Controller runtime records need not be Flecs entities merely to express this ownership.

Physical Systems remain Flecs child entities with capability components. `ChildOf` is authoritative for physical parentage. Stable domain IDs identify saved/networked entities and Controller addresses; Flecs handles remain runtime-local. Any equipment-handle index is derived and rebuilt, not a second ownership model. Existing duplicated ship IDs should be removed or justified during migration rather than extended into the new Controller model.

## Blackboards and the common interface

The entity blackboard contains explicitly shared observations and entity-wide messages. Every Controller AI on that entity can read it, corresponding to the shared Viewscreen available to humans. Entity-wide scope does not mean fleet-wide or globally visible.

The Console blackboard contains permitted observations, accepted command sections and addressed messages. Its GUI and Controller AIs consume the same permitted information. Co-hosting Consoles at one Station does not merge their boards. AI private memory is not part of either board.

Separate observation and command APIs even if they share a containing component:

```text
entity.observations.*
entity.messages.*
console.observations.engines.port.*
console.observations.engines.starboard.*
console.commands.propulsion.throttle
console.messages.<controller-id>.*
controller.private_memory.*
```

These paths illustrate typed addresses, not a requirement for per-tick string lookup. Compile authored names and schemas to typed bindings at load time.

Only supported observation producers write observations. GUI actions and AI tree actions submit commands through the same validation/commit API; they cannot assert physical success by editing the observation board. Retain evidence source, time, revision, uncertainty, withdrawal and scope. Direct machinery telemetry and contact projections must become explicit permitted observation outputs rather than parallel privileged inputs to the GUI or AI.

Requested settings, actual execution and reported outcomes remain distinct. A Controller can read accepted settings to reason about intent without treating them as achieved work. Renderers may retain their physical scene pipeline, but it must not become an alternate source of operational knowledge. Shared operational overlays and message text on the Viewscreen use the entity-board projection.

## Controllers, command ownership and bindings

Each Controller belongs to exactly one Console and owns one command section. Each command and native operation has one effective operator. One shared widget presents the Controller, including named split parts; human input and the same continuously evaluated AI policy pass through per-command admission. Reading a command section grants no writing permission.

Physical inputs bind to Controller outputs:

```text
One throttle:
  Helm / propulsion / throttle -> port engine throttle
                              -> starboard engine throttle

Two throttles:
  Helm / port_propulsion / throttle -> port engine throttle
  Helm / starboard_propulsion / throttle -> starboard engine throttle
```

One output may have multiple consumers. Each physical command input has one source. Reject duplicate/conflicting bindings; any future blending requires an explicit supported mechanism. A System may consume distinct inputs from different Controllers, such as propulsion demand and an Engineering operating limit. The mechanism defines how those different inputs interact; neither overwrites the other's command.

Settings are persistent typed values read non-destructively by all subscribers. Discrete requests have stable identities, explicit execution ownership/recipient semantics, admission receipts and outcome state. Binding one request to several mechanisms must not accidentally multiply a one-shot effect. Initially require a single executor for one-shot requests unless a supported operation explicitly defines fan-out and its receipt aggregation. Retries, replay and restore cannot duplicate accepted physical work.

Commands are committed before resource demand calculation. Machinery computes demand, receives actual grants and executes supported work. Two engines consuming 50% throttle may produce different output. There is no guarantee of execution implicit in a command write.

## Complexity and operator lifecycle

An authored Console profile selects a star rating. Each shared widget maps that rating to its own human command and native-operation responsibilities. An Automated level is read-only and AI-controlled. The human selects it in the lobby or that Console's settings menu. There are no individual delegation toggles and no per-System complexity editor. Station-level Simple/Middle/Detailed descriptions in the GDD describe workloads implemented through these Console profiles.

Keep the selected profile separate from operator availability. An unoccupied, disconnected or explicitly Away Console runs all its Controllers with ordinary AI. A fully AI Console uses the same runtime with all effective operators set to AI. An AI entity is simply an entity whose Consoles are all AI-operated, regardless of player-slot eligibility.

A profile change or handover is an atomic tick-boundary authority transition. Preserve the shared policy continuation, cancel outgoing selected actions and officer commitments, invalidate pending writes from the previous operator generation, and clear held inputs on channels that change owner. AI-owned inputs that retain their owner remain intact. Preserve accepted settings, ongoing physical work, Console evidence and addressed message history unless a particular operation specifies a change. Returning to human operation restores the selected profile's allocation, not a stale snapshot of old machinery settings. Complexity transitions cannot create two writers.

## Command ingress and trusted execution

Network adapters validate sender identity, Console leases, sequence numbers, requested destinations and permissions before passing commands to gameplay. Replicated command frames are also network input and retain those checks at each receiving ship server. Client packet formats are unchanged.

Internal code is trusted. `ConsoleCommandContext` carries the resolved claimant and Console address without network credentials; gameplay uses that address rather than interpreting packet routing fields again. Local hosted UI dispatch uses its assigned route without presenting a lease token, retaining the claimant handle to invalidate queued input after release/reclaim. Route-based World convenience calls resolve the current Console context before entering the same execution path. Older local replication records continue through packet admission.

Gameplay admission still checks current ownership/allocation, Controller generations, authored values, resources and operating conditions. Queued meta commands retain their resolved context and check ownership again at execution; Controller proposals retain their existing generation checks. These checks prevent stale work after a handover rather than authenticating internal code. Saved display sources are validated when loaded.

External AI responses have their own ingress in `InferenceConnection`: pairing, peer/packet identity, epoch, request correlation and offered choices are checked there, and the adapter attaches its retained request snapshot. World decision admission retains provider configuration, sequence, expiry, generation and candidate/state checks because a valid response can become stale before execution. Replicas still check that incoming AI frames belong to the sending ship server.

## State-tree execution

Move command trees from equipment instances to Controller instances. Each Controller policy receives typed bindings to its Console observations, accepted commands, entity-wide observations and messages, addressed inbox and private memory. It cannot inspect arbitrary world components or another Controller's private continuation.

The authoring editor exposes those available variables and supported actions. Command actions write only the Controller's own section through admission. Message actions address Controllers or publish entity-wide. Cross-Controller requests do not transfer authority. Reuse the existing hierarchical evaluator, inheritance, references, private continuation and deterministic internal later-write semantics. Later tasks can inspect earlier intent writes in the same evaluation; physical effects occur afterwards.

Scenario and job/workflow contexts remain separate from Controller decisions. A scenario publication does not itself engage machinery. This migration does not add the GDD's unimplemented general job framework.

## Always-active reporting and Coordination

Physical Systems retain factual reporters, independent of Controller AI and operator availability. Reporters observe legitimate System state, accepted settings and outcomes. They may report “temperature is approaching the limit” or “requested output is power-limited”; they do not editorialise, invent human intentions or issue assistance requests on a human's behalf. Requests/proposals belong to Controllers or explicit human actions.

Use deterministic change detection, debounce and recovery hysteresis to avoid repeating unchanged reports every tick. Reporter state persists across handover and save/resume. This is reporting behaviour, not a replacement decision-making AI attached to equipment.

A message carries an identity, typed source (System or Controller), destination, semantic type/payload, provenance, importance, validity/replacement metadata where applicable, and localised presentation template/parameters. A report remains distinguishable from a request, claim or intent. Presentation text and machine-readable meaning describe the same message.

| Destination                         | Delivery                                                     |
| ----------------------------------- | ------------------------------------------------------------ |
| Specific Controller, AI-operated    | Addressed Controller inbox, available to its state tree      |
| Specific Controller, human-operated | Text and accessible notification on its Console              |
| Entity-wide                         | Entity blackboard and shared Viewscreen text                 |
| Important AI-to-AI message          | Target inbox plus an entity-wide publication of that message |

Retain Controller addressing and history across handover; delivery uses the current effective operator. A message for one Controller does not become actionable by every AI on its Console. Define importance deterministically through supported metadata/policy, not interpretation of the prose. Automatic entity-wide publication exposes only the message's publishable payload, never an entire private Console record. Verification must ensure a message eligible for mandatory important-message publication has a valid public payload.

Entity-wide messages may be read by all Controller AIs on the entity and seen by humans through the Viewscreen. The Viewscreen keeps its world presentation and Settings access, with supported shared information/message overlays; it is not a specialist override panel. Accessible text presentation must preserve the same scope.

These are local Coordination rules. They do not implement inter-entity Comms, signal propagation or interception; those retain their separate GDD constraints.

## Deterministic tick contract

1. Apply committed operator/profile changes and invalidate obsolete operator writes.
2. Capture permitted observation/message snapshots for Controllers.
3. Collect admitted human proposals and evaluate effective AI Controllers in stable order.
4. Commit validated command sections. Internal tree intent ordering does not imply same-tick cross-Controller communication.
5. Resolve physical input bindings and calculate mechanism demands.
6. Allocate resources and execute physical effects in stable order, including shared-body ordering.
7. Produce scoped observations and factual reports from actual execution.
8. Route due messages and publish shared records; expose updated human projections and next-tick AI observations.

Explicit delays/expiry still apply to messages. No Controller gets newly produced physical results early because of ECS iteration order. Preserve transactional restore, stable logical IDs and deterministic native/WASM continuation.

## Relationship to the previous design

The old “one effective operator and command AI per System” rule is replaced by three invariants: one effective operator per Controller, one owner per command section, and one binding per physical command input. System entities remain the physical execution units. Controller identity is independent of equipment count, allowing shared throttles and independent throttles without changing engine code.

The previous implementation's System-owned trees, individually toggled delegation, direct AI action handlers and parallel GUI/AI telemetry paths are migration inputs, not exceptions to the target architecture. See the staged [refactoring plan](controller-refactor-plan.md).
