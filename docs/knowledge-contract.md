# Knowledge, publication and Coordination contract

`phoenix.knowledge` owns typed evidence, deliberate sharing and structured same-ship messages. `knowledge_definitions.cpp` provides only schema compilation/name binding for content and Workshop; `knowledge.cpp` provides authoritative runtime, delivery and persistence and belongs only in simulation-linked targets. It never reads a world, scene body, camera, renderer, machinery component or player claim. The authoritative adapter supplies permitted observations and updates control bindings at tick boundaries. This separation applies equally to human, AI, open and closed Consoles.

## Authored definitions and compiled keys

The production loader calls `compile_knowledge` at the content boundary. Numeric keys are one-based positions in the resolved definition. Trees/widgets compile their named bindings with `role`, `schema` and `field`; they retain numeric keys during play. Integers carry authored fixed-point units, booleans carry supported flags and text carries identifiers or permitted reported content. Runtime decisions do not interpret JSON.

```json
{
  "history_limit": 256,
  "roles": ["helm", "engineering", "scenario"],
  "schemas": [
    {
      "id": "power-request",
      "producers": ["helm"],
      "readers": ["helm", "engineering"],
      "ship_publishable": true,
      "stale_after_ticks": 100,
      "fields": [
        {
          "id": "requested-power",
          "type": "integer",
          "required": true,
          "publishable": true
        },
        {
          "id": "reason",
          "type": "text",
          "required": false,
          "publishable": true
        }
      ]
    }
  ]
}
```

Identifiers, roles, duplicate definitions, field types, producer/read roles and bounded history settings are verified. Fields default to required and private; schemas default to Console-only and no automatic stale interval. A zero stale interval means that this schema supplies no automatic stale classification, rather than inventing a refresh. Saved state embeds its resolved contract and refuses a changed key/type/permission contract.

The gameplay adapter registers stable entity, Station, Console, physical System and Controller identities. Console bindings supply reader roles and human presence, including observation-only Consoles. Physical Systems register as report sources with an observation-owning Console. Only Controllers register as AI decision instances; their full entity/Console/Controller identity and current effective operator determine addressed delivery. Physical subscriptions do not grant observation access or transfer report ownership. Runtime registration rejects conflicting ownership.

## Evidence and boards

`KnowledgeRecordInput` contains a schema/producer key, subject, typed fields, withdrawal flag and provenance: source identity, observation tick, monotonic revision, nonnegative uncertainty and fact/claim/intent/prediction kind. Producer permission and field types are checked. Ordinary Console ingestion requires all required fields and permitted reader roles. The adapter is responsible for proving that the producer actually possesses this observation; a producer role is an integration capability, not an untrusted client-supplied permission.

Record identity is scoped by board, owner, schema, subject, source and publication stream. Each explicit publication/feed has an independent stable stream identity while preserving the original provenance. Concurrent publications of different field subsets therefore remain independent, and stopping one freezes only its own dated evidence. Equal-version subsets encountered while composing one stream merge only non-conflicting permitted fields. The pair `(observation tick, revision)` must increase; delayed old reports and duplicates cannot replace newer evidence. Different sources retain distinct claims. A withdrawal is a newer tombstone and does not erase history or physical work. `read_console` and `read_ship` return permitted dated records in insertion order, including retained history and withdrawals. Consumers select the latest record per key (including publication identity) and respect its evidence kind and stale status; absence of a new report never becomes hidden current truth.

Retention bounds superseded evidence per board and message history per Console. Latest evidence and latest withdrawal for each distinct record key remain available, so history eviction cannot allow an old report to become current again. These are storage limits, not gameplay message bandwidth or charging resources. Authored entity/record populations and tree work remain bounded by their owning simulation contracts.

`private_memory(ai)` is independent for every AI and is never returned by Console/shared read APIs. The tree adapter exposes only that instance's supported private values. Co-hosting Consoles does not merge either board or memory.

## Publications and scoped feeds

`publish(console, schema, subject, fields, live, tick)` deliberately copies an explicit permitted field subset to that Console's ship board. Ship publication requires the schema and every selected field to allow it. The source must already be readable by the publishing Console. A non-live publication requires existing evidence; a live publication may wait for its first observation.

Once explicitly published, those selected fields are visible to every reader on the same entity, including Consoles without the private schema's reader role. This is the common entity blackboard/Viewscreen contract. It does not expose the source's private Console board, unselected fields or another entity's board.

`feed(sourceConsole, targetConsole, schema, subject, fields, tick)` establishes a scoped same-ship live feed. It checks both Consoles' reader permissions and every field's share permission. A Console-only schema may be shared through a scoped feed while remaining forbidden on the ship board. This copies evidence directly, without feeding it recursively through other publication rules.

Live publications follow subsequent supported writes from the declared source Console and subject. They preserve the original producer, source, observation time and uncertainty; they do not manufacture a fresh observation time. `stop_publication(id)` stops refresh and retains already delivered dated evidence. A permitted direct scenario/objective producer may call `write_ship`; this still checks ship publication and field permissions. No API automatically pools contacts or derives awareness from a Viewscreen.

## Structured Coordination

`send(CoordinationInput, tick)` validates a typed payload and captures its System or Controller source, originating Console/entity and human/AI/scenario/reporter origin. Localised title/body keys, string substitution parameters, urgency 0–3 and the automatic-presentation flag remain separate from the semantic payload. AI evaluates payload fields; it never parses presentation prose.

Destinations resolve against registered ownership:

- Controller: its explicit Console and Controller ID on the originating entity, with effective operator checked at delivery. Physical System destinations are rejected by explicit gameplay layouts because Systems have no decision inbox.
- Console: that exact Console.
- Station: every member Console of that exact Station on the sender's ship, once per Console.
- Ship: an explicit broadcast to all registered Consoles on the sender's ship, once per Console.

Unknown, ambiguous or cross-ship targets fail. They never widen to broadcast. Every resolved recipient must be permitted to read the payload. The ordinary authority adapter must validate that the caller controls the asserted sender and may request that message; Coordination grants no actuator authority.

An authored message may opt into `"human_seeking":true` (default false). At delivery, its addressed Controller/Console/Station/Ship recipients retain ordinary routing. The runtime additionally offers the work to currently occupied, non-Away Consoles on that ship whose reader roles permit the schema, including occupied Consoles whose Controllers use AI. A human arriving during the delay can receive the offer; a human leaving before delivery does not. Recipients are deduplicated, so an addressed Console receives one history item and at most one notification. The existing automatic human-to-human popup suppression still applies.

Extra human observers receive `offer_only` history and eligible notification entries. Those offers do not write their Console board, enter any AI inbox, or become AI work when the Console later goes Away. Offers never change claims, complexity profiles, Controller operators or actuator permissions. The transport policy and offer-only history markers survive save/restore, while historical popups remain suppressed on reconnect.

Message template `fields` and presentation `parameters` accept typed bindings such as `{"argument":"amount"}`, `{"argument":"flag"}`, `{"argument":"detail"}`, `{"observation":"command.speed"}`, `{"intent":"speed_mm_s"}`, `{"memory":"desired_output"}`, `{"parameter":"report_kind"}` and `{"elapsed":true}`. The compiler retains each declared source and name; a presentation parameter's output name does not choose its source. Scalar field types must match the schema. Literal-template calls are checked against their emitting tree, including referenced trees and contextual observation slots. Dynamic template selection still validates actual values at admission and fails if the selected template needs unavailable context.

Send actions capture their active frame's memory, parameters, intents and elapsed time at emission. Later memory writes, intent changes, exits, and referenced-tree completion cannot change an already emitted message. `evaluate_gameplay_message` combines those typed captures with the immutable permitted observation snapshot; it never reads the World or dispatches JSON expressions during a tick. Human-authored template commands have their supplied amount/flag/detail values, and fail closed if a template requires unavailable AI context. `request_key_name` preserves the authored identity for subsequent withdrawal actions. Controller `publish.<template>` actions use the compiled owning-Console publication contract. `stop_publication` uses its returned integer handle; `withdraw_request` uses the Controller's request key. Neither grants a producer API for new facts.

Capturing message context consumes the same transactional tree evaluation budget as task execution. Each retained field costs one work unit plus one unit per 64 bytes of its copied name and text. The runtime charges this before allocating captured maps; exhausting the shared budget returns no effects and preserves the previous continuation.

Delivery tick is `sent tick + max(1, authored delay)`, with overflow checking. Delivery order is `(delivery tick, stable message identity)`. `advance` only moves forward, returns newly delivered records once and inserts permitted payloads into recipients' boards. New messages do not execute recursively inside the same tick. An authored expiry must be later than the first delivery tick; overdue pending messages are discarded at expiry.

At delivery, AI recipients remain silent, AI/scenario-to-human messages notify, and automatic human-to-human popups are suppressed. Explicit non-automatic human messages may notify. Controller destinations use that Controller's current effective operator plus Console presence, while Console/Station/ship destinations use current Console presence. Important automatic AI-to-AI deliveries and explicit entity-wide messages additionally enter the public board only after publishable-payload validation; public copies cannot acknowledge or accept private work. `message_history` always clears notification flags; reconnect/history projection cannot replay historical popups. The adapter presents only fresh `advance` results as new announcements.

Each Controller has an independent receive cursor. Gameplay peeks at permitted actionable deliveries for that Controller and explicitly acknowledges handled messages; another Controller's addressed message cannot become its work. Human and AI handover retains message identities and history. The generic knowledge domain also exposes `receive(ai)` for consuming adapters, but live Controller trees use peek/acknowledge.

Nonzero request keys replace pending transport and mark already delivered requests from the same typed sender/key as superseded. `withdraw_request` cancels pending delivery and marks retained matching requests withdrawn. History remains visible with `actionable=false`; `receive` skips withdrawn, superseded and expired requests, advancing the independent cursor past them. Expiry is evaluated against current domain time. Accepted physical work is untouched and has its own cancellation boundary. New withdrawn/updated intent can be sent as a newer typed record when recipients must learn that change.

`status_change(producer, statusKey, value, tick, debounce, recoveryHysteresis, minimumGap)` supplies persistent report latches. The first state reports once; subsequent asserted/recovered transitions require stable intervals and report spacing. The producer chooses supported limited/recovered semantics and sends ordinary typed messages when the latch reports. There are no voices or inter-ship communications in this domain.

## Persistence and production integration

`save` format version 3 records Controller identities alongside entity/Console/System bindings, operator flags, private Controller memory/cursors, evidence/history, publications, queues, request supersession/withdrawal and reporter latches. Generic runtimes without Controllers can still use version 2; World/session continuation uses the stricter schema-6 boundary described in [Controller content](controller-content.md). `restore` validates into a temporary runtime before replacing live state. It rejects incompatible contracts, invalid ownership, permissions, fields, provenance, identities and receive cursors. Unsigned JSON identities, keys, ticks and counters reject negative, fractional and overflowing values. Restored messages must resolve to their exact authored destinations and recipient list, have valid urgency/delay/expiry, and agree with identity counters. Duplicate deliveries, conflicting broadcast copies and multiple actionable requests sharing one replacement key are refused. Saved live human flags must be replaced from recovered authority/claims before subsequent delivery; pending messages retain sender origin while receiving control is current.

Production integration must compile pack definitions, register resolved ownership, ingest only permitted observations, expose scoped projections, update authority flags before delivery, route fresh notifications through localised/semantic controls and save this domain with world checkpoints. Resource/navigation controllers submit ordinary messages and act through admitted machinery commands. Scenario/objective code explicitly publishes allowed records. Synthetic integration cases should exercise these adapters through the real production loader and Flecs world, including independent consumers, delayed handover, stale reports, limited-field feeds, cross-ship refusal and save continuation.

## World integration adapter

`src/simulation/gameplay_knowledge.hpp` is a private declaration included after the `phoenix.simulation` imports. Its implementation is another unit of that module. It contains no renderer or world lookup; World calls it from the existing admitted-command and observation phases.

The compile-only structures and parser live in `src/content/gameplay_knowledge.hpp` and `.cpp`, exported from `phoenix.content` as `compile_gameplay_knowledge(Catalog)`. Content verification and Workshop call the same compiler that the World adapter uses, including for unused Console/message/equipment definitions. Player and Workshop targets link this compiler through content; they do not link the authoritative knowledge runtime. The compiler verifies supported telemetry names, required field coverage, roles, source scopes, sharing fields and fitted destinations before a World is created. Restricted presentation bundles omit this authoring graph and use their ordinary UI verification instead.

New human and AI messages resolve producer permission from their Controller source. Selection follows the schema's authored producer order and chooses the first role actually held by the source Console, then verifies that Console owns the sender and the asserted human/AI origin has effective control. A schema's first producer is never assumed to belong to every sender. No authorized intersection fails admission without transport effects. Content verification checks Controller templates against their bound sender, and checks that each listed entity template has at least one permitted source Console able to originate it. Relaying known evidence retains its original producer and provenance instead of selecting a new origin role.

- Call `configure(catalog)` once after resolved content verification. It compiles Console roles, ownership, System telemetry records, message templates, scoped feeds and named publication templates. `register_ship(stableEntity, definitionName, tick)` creates independent runtime identities and starts only authored scoped feeds. Publication templates remain inactive until a supported action activates them.
- Console slots declare `knowledge_roles`; Console definitions may provide defaults. `knowledge_producers` optionally narrows the roles permitted to originate new reports and otherwise uses the Console roles. Closed and AI-operated Consoles receive normal permitted ingestion because this registry is independent of live player claims.
- Before delivery, update every Console with `set_console_presence` and every Controller with `set_controller_control`. Claims, profiles and Away state determine these values. Observation-only Consoles still have presence. Recovery clears device authority while preserving operational knowledge and continuations.
- `ingest_system` accepts a typed record whose source is `system_id(ship, fittedName)`. It validates the owning producer and delivers only to that owner or explicitly authored System-feed recipients with permitted reader roles. `ingest_console` accepts supported Console observations from the trusted observation producer. Neither accepts arbitrary hidden truth from a player command.
- `publish`, `activate_publication`, `activate_feed` and `stop_publication` validate source ownership and field/read scopes. The source address for a player action must come from World claims, and the source for AI actions must come from the fitted tree context.
- `admit_message` checks the owning source Console, current human/AI authority and producer scope. New claims, intents and predictions identify their own sender as source. A factual message, or a relay of another producer's evidence, must match an actual permitted Console/shared record, preserving provenance and selected fields. `publish_scenario_message` and `publish_ship` are separate trusted scenario APIs; they must never be exposed as player-command shortcuts.
- Message templates compile typed literal fields, required dynamic field/parameter keys, destination names, presentation metadata, delay, expiry and request-key identity. The tree/action adapter evaluates supported dynamic bindings before `admit_template`; no JSON expressions execute during delivery. `message_definition` and `request_key` expose the compiled contract for binding.
- At tick start, call `tick(tick)` after authority changes. It returns only newly delivered messages. `peek_controller` exposes actionable deliveries without consuming them; `acknowledge_controller` advances only that Controller cursor. Private Controller memory and physical reporter latches remain outside player projections.

### Telemetry bindings

An individual fitted System may declare records alongside its ordinary equipment properties:

```json
{
  "records": [
    {
      "schema": "engineering-status",
      "producer": "engineering",
      "subject": 0,
      "kind": "fact",
      "fields": {
        "stored": "stored_energy",
        "temperature": "heat",
        "limited": "supply_limited"
      }
    }
  ]
}
```

Zero subject binds to the owning ship. Schema, producer, fields, types, required-field coverage and Console producer scope compile once. Telemetry source names are interned numeric slots in `telemetry_names()`. The production mechanism adapter supplies only its supported readings to `ingest_telemetry` using a vector of optional typed values in that layout. A map-of-integers overload is available at the mechanism boundary; it converts only declared telemetry names and accepts zero/one for Boolean schema fields. Missing required readings fail rather than inventing data. Optional readings can remain absent. Observation time and revision use the committed observation tick; ingest each supported System snapshot once in the observation phase.

### Objectives and player projections

The production loader reserves the `phoenix.crew` and `phoenix.scenario` roles and `phoenix.objective` schema before compiling content. Authored content cannot replace these built-ins. The schema has `id` (integer), `status` (text), `title` (text), `region` (text) and `credited` (Boolean), plus optional integer `destination_x`/`destination_y`, all publishable, with the scenario producer and crew readers. When present, the adapter appends `phoenix.crew` to every Console's reader roles. Scenario execution supplies actual objective state to `publish_ship`, or to `publish_scenario_console` for a restricted recipient. The latter checks recipient readability without granting that Console the scenario producer role. Publication never changes Helm control or confers physical completion credit.

`project(address)` returns only that Console's records, its permitted shared records, its message history and eligible notification snapshots. `project_json` converts those typed values into names/fields for the existing wire/UI boundary and adds `stale_after_ticks` beside observation times. It contains no other Console boards, queues, private memory or trees.

`notifications` contains sequence/title/body/parameters/urgency only for deliveries eligible to notify a human. These snapshots are bounded by the same history storage policy and excluded from save/recovery. A presentation route establishes its initial sequence baseline and announces later sequences once; ordinary history refresh and reconnect do not become new alerts.

The adapter save includes dynamic ship-definition/name bindings, identity allocator, publication owners and the complete knowledge runtime. Restore checks those identities against the currently compiled Console/System/Controller ownership, restores the runtime and clears human authority. World must validate the saved dynamic entity roster separately and apply fresh admitted claims before delivery.

## State-tree record observation bindings

`gameplay_record_bindings(tree, catalog)` compiles optional record observations from schema metadata. `gameplay_registry(tree, catalog, context)` appends them to the normal gameplay vocabulary before the ordinary state-tree compiler resolves observations. Both functions live in `src/content/tree_bindings.cpp`, with no simulation dependency or record-value access.

```json
{
  "id": "example.destination-policy",
  "context": "controller",
  "record_bindings": [
    {
      "name": "known.destination-x",
      "scope": "ship",
      "schema": "phoenix.objective",
      "field": "destination_x",
      "subject": "self"
    },
    {
      "name": "known.contact-report",
      "scope": "console",
      "schema": "contact-report",
      "field": "speed",
      "subject": "selected_contact"
    }
  ]
}
```

Bindings inherit through ordinary tree `extends`. A child binding with the same name replaces that complete binding while preserving its registry slot; new names append. Repeating a name in one definition is an error. Missing parents, cycles, excessive depth, unsupported scopes, unknown schemas/fields, unpublishable ship fields and collisions with built-in observation names are refused.

The observation type derives from the field's integer/Boolean/text type. An optional authored `type` must match. Subject defaults to `self`; `selected_contact` uses the permitted Console selection in Controller context; a positive integer selects an explicit subject. Controller bindings also support `latest` for the newest permitted record subject, with evidence metadata and withdrawal preserved. The compatibility spelling `subject_parameter` accepts empty/self/selected_contact and cannot be combined with `subject`. Compiled metadata uses empty `subject_parameter` for self, `selected_contact` for the permitted Console selection, and decimal digits for a literal identity. Arbitrary private-memory or undeclared parameter lookup is unsupported.

The World fitting adapter verifies the controlling Console's read roles and resolves values only from an immutable permitted Console or ship-board snapshot. Missing, withdrawn or stale records produce the supported missing observation according to the consuming policy; a binding never refreshes evidence from raw machinery, contacts or scene state. Scenario bindings retain their separate scenario context, and selected-contact selection does not become scenario omniscience.

The compile-only verifier checks each Controller tree's resolved bindings against its actual owning Console reader roles. An unreadable schema is a content error even when the corresponding observation would otherwise remain missing at runtime.

Approved authoritative inspection can call `pending_messages(limit)` on the runtime or World adapter. It returns a typed copy ordered by delivery tick and message identity, defaults to 32 entries and clamps requests to 256. Snapshot construction retains at most that many message copies, exposes no private AI memory, and never consumes or delivers a request. Ordinary Console projections continue to exclude pending queues.
