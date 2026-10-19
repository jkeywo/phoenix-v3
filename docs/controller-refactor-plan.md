# Console Controller refactoring plan

Approved target · 19 October 2026. **Status: implemented and verified; all seven delivery steps complete.** Implements the [agreed architecture](controller-architecture.md) and revised [GDD](GDD.md), preserving existing physical capabilities and deterministic execution. Historical gameplay completion receipts describe the earlier architecture.

## Starting code and required changes

This table records the pre-refactor implementation used to scope the work. Removed names are retained here to make the migration audit traceable.

| Current code                                                                                                                                   | Migration                                                                                                                                                                                                        |
| ---------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/simulation/equipment.hpp`: `Equipment.held`, `BrainRuntime`, `SummaryIntents`, `ActionResult`, `EffectJournal` on equipment               | Move operator/decision continuation, command intent and Controller effect receipts into Console-owned Controller runtime. Keep physical execution receipts and reporter state with their actual execution owner. |
| `src/simulation/world.cpp`: `brain_observations`, `brain_output`, `run_equipment_controllers`, `refresh_control`, `meta`, `operate`, `observe` | Separate Controller evaluation/admission, command binding/consumption, observation production and projection. Remove direct Controller-AI component mutation and per-System human delegation.                    |
| `src/behavior/state_tree*`, `gameplay_vocabulary*`                                                                                             | Retain generic evaluator; replace System decision context/bindings with Controller identity, board reads, owned command writes and Controller-addressed messaging. Keep scenario context separate.               |
| `src/knowledge/*`, `src/simulation/gameplay_knowledge*`                                                                                        | Replace System-as-AI identity with Controller registration/inboxes; distinguish System report sources from Controller message sources; supply common human/AI observations and entity-wide presentation.         |
| `src/content/stations.cpp`, `equipment.cpp`, `tree_bindings.cpp`, `gameplay_knowledge.cpp`, `content.cppm`                                     | Compile Controller definitions, command sections, physical input/output bindings, profile allocation and publication rules.                                                                                      |
| `src/core/console*`, `contracts.cppm`, `session/api.cppm`, `session/execution.cpp`, local/fleet session and network projection/packet paths    | Address Controller commands; preserve route/lease authentication, sequence checks, bounded projections and committed execution.                                                                                  |
| `src/tools/gameplay_editor.cpp`, content Workshop paths, `simulation/workshop_test*`, `presentation/*`, menu/lobby paths                       | Author and inspect new bindings; select profile in lobby/Console settings; expose shared Viewscreen messages and accessible controls; remove individual delegation UI.                                           |

Flecs already stores physical Systems as child entities queried by capability. Retain that work and stable shared-body execution order. Do not move physical simulation back into Console objects or interpret JSON in the tick loop.

## Migration policy

The target architecture is agreed; concrete C++ storage names and schema version numbers are implementation choices. Choose and document new content/save/wire versions in step 1. Existing schema 1–4 saves and old wire/content shapes must never be silently interpreted as Controller state.

Use boundary conversion for old definitions where the mapping is unambiguous: generate an explicit Controller/command section for an old System policy and bind its physical inputs. The live runtime must still use only the new model. Convert legacy timed entity policies only where every output has an explicit Controller owner. Reject unsupported or ambiguous conversions with actionable diagnostics; do not retain a special legacy NPC execution path to make them load.

Saved continuations require more than structural content conversion. Migrate only where program identity, private memory, pending messages, command ownership and effect receipts have a defined mapping; otherwise refuse the old save without modifying the running session. Refuse incompatible peers before play. Historical old-schema fixtures can test those explicit conversion/refusal boundaries rather than require two execution engines forever.

No changes to V2 or reference snapshots. Probes, stations, new gameplay mechanisms, inter-entity Comms and a general job framework are outside scope. Generic controllable-entity fixtures verify the absence of ship-only assumptions without implementing those products.

## Delivery steps

Each step is a verified logical commit. A transitional adapter may support incremental integration, but the final gate requires removal of bypasses. Do not mark a step complete from compilation or an editor shell alone.

### 1. Typed contracts and production compilation

Define stable entity/Console/Controller addresses, owned command schemas, persistent settings versus one-shot requests, typed physical input bindings and observation producers. Add controllable layout and Console complexity profiles. Decide schema transitions and document exact compatibility/refusal behavior.

Compile names to typed indices/handles; validate unique Controller ownership, exactly one source per physical input, type/range compatibility, valid profile coverage, message target/source roles, public payload eligibility and tree read/write scope. One output may feed multiple compatible physical inputs. Unsupported request fan-out is refused explicitly. Validate unused templates and resolved inheritance/assembly placements.

Acceptance: synthetic production-loader fixtures resolve shared and separate throttles, repeated equipment and distinct Console identities; malformed ownership, profile, input binding and message publication configurations fail with useful diagnostics. No change to shipped tuning assertions.

### 2. Controller runtime and common command commit

Introduce Console-owned Controller instances and operator generations. Move decision trees/private continuation off physical equipment. Replace direct AI writes with the same admitted typed command proposals used by human input. Commit sections at the tick boundary, then bind physical consumers to committed settings. Preserve existing resource quotation, funding and execution paths.

Implement stable request IDs and single-executor receipts for supported discrete actions. Keep physical work independent of Controller interruption. Remove or route every special AI path (`SetDestination`, follow, Match Speed and resource operations included) through the common interface. Temporary compatibility translation belongs at load/input boundaries, not in actuator execution.

Acceptance: the same command produces the same physical result under a human and an AI operator; one throttle drives two independently constrained engines; two throttles stay independent; duplicate input bindings and unauthorised section writes fail. Repeated requests do not repeat physical effects. Shared-body ordering and resource conservation remain covered by existing synthetic flight/resource journeys.

### 3. Shared observation path and Controller trees

Make supported telemetry, own motion, sensor observations and action receipts explicit permitted observation outputs. Both human projections and AI input bindings consume those outputs and the accepted command board. Remove direct `brain_observations` world/telemetry reads and parallel operational `World::observe` contact/telemetry construction. Keep spatial detection/physical scene work in its own capability processing; publish its permitted results through the common observation boundary.

Adapt tree vocabulary/editor bindings to entity/Console boards, own command section, addressed messages and private memory. Preserve inheritance, reference interruption, ordered internal writes and bounded evaluation. Freeze decision inputs per tick; post-execution observations become next-tick AI inputs. This unifies access without promising the full future sensor-estimation design.

Acceptance: human and AI readers receive equivalent permitted facts, provenance and withdrawal/staleness handling; another Console/entity's private data cannot leak through telemetry, contacts or semantic UI. Tree writes cannot manufacture observations. Save/restore preserves Controller continuations and shared-setting state.

### 4. Factual reporting and Controller-addressed messages

Register physical reporters independently of Controller AI. Preserve event/change latches, debounce and recovery hysteresis across human operation and saves. Reporters emit facts only. Controller AI and human actions originate requests/intents through authorised messaging.

Replace System destinations with Controller destinations, retaining stable identity, delayed delivery, expiry/replacement/withdrawal and per-recipient consumption. Implement entity-wide messages and deterministic additional publication of important AI-to-AI messages. At delivery, use the current operator; retain unhandled addressed history across handover. Bound histories and projection sizes with visible omission information as today.

Acceptance: a human-operated engine still reports heat to an AI Controller; the same addressed message reaches a human Console after handover without losing identity or duplicating actions. Important AI-to-AI and explicit entity-wide messages reach the entity board; ordinary addressed messages do not silently publish private payloads. Reporter resume does not replay an unchanged warning as a new event.

### 5. Complexity, lobby/settings and full autonomy

Replace per-System delegation state/actions with selected Console profile plus operator availability. Expose profile selection in lobby and Console settings, and remove individual delegation controls. Implement atomic operator-generation transitions, cancellation of obsolete queued writes and safe held-input release. Preserve accepted settings and physical work; returning humans regain their selected profile allocation.

Ensure unclaimed, Away and disconnected Consoles assign all Controllers to ordinary AI. A non-player-slot entity and an unoccupied player-slot entity with equivalent configuration must execute the same Controller/physical pipeline. Keep server voting/slot admission separate from gameplay operation.

Acceptance: profile changes, disconnect/reconnect and Away/return never admit two writers or retain stuck input; selected profile survives recovery. Fully AI and mixed Consoles work through the same path. Synthetic generic entities need no Helm or human claim to run their supported capabilities.

### 6. Authoring, presentation and remote continuation

Complete Workshop editors for Controllers, command sections, physical subscriptions, observations, factual reporters, message targets and complexity profiles. Expose inherited bindings, validation failures and live Controller/operator/request state in isolated Test. Convert maintained production content to the explicit model; do not infer feature completion from its configuration.

Bind Console widgets and accessibility actions to Controller commands and shared observations. Add supported entity-board information/message presentation to the Viewscreen with readable history/urgency and equivalent accessible text. Preserve separate device routing and localisation. Verify no former per-System delegation affordance remains.

Update client-safe content/projections, lease/sequence admission, wire compatibility, portable saves, checkpoints and replay. Persist Controller continuation, command sections, request receipts, reporter latches, message cursors, profile selection and scoped evidence; never persist live device authority as a valid recovered lease. Rebuild derived Flecs indexes on restore. Continue to reject malformed state transactionally.

Acceptance: Workshop edit/verify/export/reopen and isolated Test use production paths. A remote client operates a shared throttle, changes profile, receives a routed report and recovers across checkpoint/rejoin without duplicated effects or widened access. Native/WASM continuations agree. Client and Workshop builds retain their authoritative-simulation dependency boundary.

### 7. Integration gate and completion record

Audit and remove old System command-tree dispatch, direct AI physical mutation, individual delegation actions and privileged operational projection shortcuts. Review remaining `Equipment.ship`/`EquipmentIndex` usage: derive parent identity from Flecs; keep any needed ordinal lookup explicitly derived. Update runtime documentation and delivery ledgers to actual implemented behavior, retaining historical receipts as history.

Run the repository formatting check, production pack verification and `scripts/check.ps1` native/WASM/continuation/network gate. Extend existing synthetic system journeys for the new boundaries rather than adding per-function tests or shipped-content goldens. Repeat broad checks only after relevant changes or failures.

Manually trigger and record GUI journeys for lobby/settings selection, shared and split throttles, human/AI handover, Console reporting, Viewscreen messages and Workshop Test. Screen-reader usability, physical simultaneous input and six-display performance remain separate manual evidence; headless checks cannot establish them. No new hardware acceptance is claimed by this plan.

## Main risks to resolve during implementation

- A single mutable board could let UI or AI falsify state: separate producer and command APIs and validate all writes.
- Distinct command sections could still target one physical input: enforce one binding per input, not merely one writer per section.
- Request fan-out or replay could multiply physical work: explicit execution ownership and durable request/effect identities.
- New reports or shared messages could reveal private state: public-payload verification and common observation projections.
- Old commands could arrive after profile changes: operator generations plus existing lease/sequence checks at commit.
- Partial migration could preserve different human/AI physics: final source audit and equivalent-input integration journeys must cover every supported action path.
- Moving state out of equipment could break saves and repeated assemblies: stable Controller identities, boundary conversion/refusal and transactional continuation tests.

## Delivery status

| Step                                                         | Status     |
| ------------------------------------------------------------ | ---------- |
| Agreed architecture, GDD reconciliation and refactoring plan | Documented |
| 1–2: compiler and common Controller runtime                  | Verified   |
| 3: shared observations and Controller trees                  | Verified   |
| 4: Controller messaging and factual reporting                | Verified   |
| 5: complexity, operator transitions and autonomy             | Verified   |
| 6: authoring, presentation and remote continuation           | Verified   |
| 7: source audit, documentation and integration gate          | Verified   |

The existing gameplay ledger remains evidence for the previous runtime. It does not establish completion of this refactor.

## Incremental receipts

These receipts record the state at each verified commit. Their references to outstanding work apply at that point in the sequence; the delivery status and final receipt record the current completion state.

### Workshop Controller authoring receipt

Entity Properties edits explicit Controller layouts, owned command sections, physical consumers and Console complexity allocations. Controller trees expose command/telemetry/peer-command/message bindings and a task palette generated from their resolved production registry. Inherited groups require explicit override; removing a local group restores inheritance. Physical reporter and Controller destination editors are available. Explicit layouts hide legacy policy and preset controls.

Isolated Test now reports bounded explicit Controller identities, operator generations, active states, transitions, diagnostics and command receipts. Independent verification built native and the Workshop test target, verified both production packs and passed a synthetic edit/export/reopen/Test journey with inherited bindings, actual command consumption and transactional refusal of an invalid replacement. Actual ImGui authoring interaction remains a manual acceptance gap. This is a step 6 foundation; maintained-content migration and the remaining integration requirements are still outstanding.

### Lobby and settings complexity receipt

Explicit-layout Consoles offer authored profiles in the lobby, local station Ready screen and both Console settings paths. Lobby ownership checks prevent changing another ship's or another player's Console. Changing a profile regenerates the prepared world and resets readiness; selecting the current profile is a no-op. Initial selections apply before the first tick and claims. Recovery retains saved allocation, with subsequent changes through authenticated in-game settings. Lobby negotiation uses `pregame-2`; direct hosts recognise pregame discovery independently of that version.

Independent verification passed the native build, Controller runtime and session suites, and the real-signalling lobby journey. The journey selects an AI profile, confirms readiness invalidation and verifies AI allocation after launch despite a human Console claim. It also verifies explicit-layout Console publication and rejects a forged Controller publication envelope. Preparation/restore tests establish saved profile retention and refusal of setup mutations after claim or tick. Actual graphical lobby/settings interaction remains pending, as do legacy runtime removal and maintained-content migration.

### Compiler foundation receipt

The first incremental change adds Controller addresses, typed setting/request channel metadata, physical consumer indices and Console profile allocations through `compile_controllable_layout`. Production loading verifies explicit layout schema 1, shared inputs, canonical enabled-channel aliases, input conflicts, fitted capability checks, speed bounds and request single-executor rules. Legacy whole-entity timed policies convert to one Controller rather than duplicate evaluation per actuator.

Native `phoenix_content_tests` passes, including inherited shared-throttle fixtures, cross-Console profile refusal and malformed input bindings. An independent verification agent also executed that suite successfully and reviewed the foundation diff. Changed C++ formatting and whitespace checks pass.

This receipt records step 1a at its commit. The runtime receipt below supersedes its temporary refusal of explicit layouts. Full tree/observation/message scope validation and compatibility migration remain outstanding.

### Explicit-layout runtime foundation receipt

Step 2a executes explicit layouts through Console-owned Controller sections. Human proposals carry Controller/channel identities, operator generation and request identity; timed AI programs use the same commit function. Physical consumers apply changed settings after commit. Profiles are selected per Console; Away, release and claim clearing change effective operators, invalidate obsolete proposals and release supported held inputs. A shared output drives multiple engines, while separate sections preserve independent throttles.

Schema 5 World saves preserve Controller command revisions, consumed revisions, request receipts, selected profiles and timed-program continuation. Restore validates operator state against claims/profile/Away transactionally. Schema 1–4 states cannot supply an explicit layout's missing Controller continuation. This is World-level support: portable session/network migration is still pending. Contextual Controller trees are explicitly refused by World until their vocabulary migration; they are not silently ignored.

Native content, simulation and new Controller runtime integration suites pass. The independent verification agent authored and ran shared/split throttle, real timed-AI/human equivalence, Away/return, clear-claims cancellation, obsolete-generation refusal, forged operator restore refusal, impulse request deduplication and deterministic restore journeys. Changed C++ formatting and whitespace checks pass.

This does not complete step 2: contextual Controller trees, compound destination/follow commands and migration/removal of the old execution paths remain. Observation/reporting, GUI, Workshop, remote continuation, production-content conversion, full native/WASM gate and manual GUI evidence are not yet established.

### Controller-context tree receipt

Controller-context trees now declare owned `command_bindings`, read accepted values as `command.<channel>`, and emit `write.<channel>` proposals through the common commit function. They use the existing hierarchical evaluator, ordered intents, references and private memory. Declared Console/entity record bindings use the same permitted knowledge projection as human readers, frozen before physical execution. Content compilation rejects foreign command declarations, mismatched channel types/semantics and forbidden record-reader roles. Physical component observations and System action handlers are unavailable in this context.

Controller continuations and bounded request-effect journals persist in schema 5. Handover interrupts outgoing trees and resumes private memory when ordinary AI control returns. Independent native integration verification covers hierarchical execution, internal intent readback, equivalent human commands, suppression during human operation, private-memory isolation and restored resume. Content and Controller runtime suites pass.

This is a further foundation, not completion of step 3: the previous System-context runtime remains for old definitions, physical GUI telemetry still needs the common observation boundary, and Controller message vocabulary/provenance-rich observations are pending. The earlier temporary World refusal applies to System/scenario contextual trees, not the new Controller context.

### Console/session command transport receipt

Projected Controller controls carry controller/channel identity, operator generation and request identity through ConsoleModel, committed GameExecution frames and network host/client commands. Descriptor matching includes those fields. Scalar physical widgets remain a separate migration item; this receipt covers the generated accessible command controls and profile selection actions. Per-Controller requests do not enter the old single-throttle optimistic UI cache.

World schema 5 is now accepted by portable local-session recovery, which clears live authority. The packet envelope is schema 3: correctly authenticated schema-2 packets are refused before play. Command JSON encoding/decoding is shared; numeric counters and identifier sizes are checked. Bounded network projections account for Controller rows with omission counts. Client assets contain no AI programs/private memory and permit observation-only physical fittings.

Independent integration verification covers ConsoleModel to LocalSession shared throttle, profile delegation, portable save/recovery with fresh claims, and committed GameExecution lease/sequence handling. Host/client targets build; native session, transfer and Controller runtime suites pass. The schema-3 authentication vector was independently calculated using Python hmac/hashlib, and the previous correctly signed schema-2 envelope is explicitly rejected. This is not the real-network checkpoint/rejoin or manually triggered GUI acceptance gate.

### Flecs parent identity receipt

Physical Equipment no longer stores a duplicate `ship` field. Stable ordering and the remaining legacy snapshot adapter derive the parent entity through Flecs `ChildOf` and its `StableId`. `EquipmentIndex` retains only derived child handles, rebuilt during spawn/restore; it is not an ownership or state store. Independent native simulation and Controller runtime suites pass after the cleanup. This completes that specific step-7 cleanup item, not the remaining migration or final integration gate.

### Shared operational observation receipt

Console observation production now has one boundary for permitted own motion, fitted telemetry and detected contacts. Human projection and AI decision snapshots consume that output; the legacy tree observation adapter no longer constructs its own physical telemetry. Snapshots freeze before decisions and physical execution, then expire at the tick boundary. Controller trees can bind permitted System telemetry through typed `system_bindings`; field types come from the producer and foreign Console feeds are refused.

Independent Controller integration verification covers two Controllers reading the same pre-execution state, next-tick agreement with human telemetry, observation-only Console isolation and invalid feed/type bindings. Native Controller, gameplay-knowledge and simulation suites pass. This is an operational observation foundation: provenance-rich Controller vocabulary, addressed messaging, shared command reads and removal of the legacy execution path remain outstanding.

### Accepted-command read receipt

Controller trees declare read-only `command_reads` with a binding name, Controller identity, channel and type. Compilation resolves only channels in the same Console and rejects unknown channels or mismatched types. Read access does not add write actions. Own `command.*` observations and peer command reads use the same accepted-command snapshot, captured before decision evaluation and discarded after the tick. Internal tree intents remain available for ordered writes within an evaluation.

Independent integration verification covers a writer and reader in one Console: the reader sees the pre-tick accepted value, then the new accepted value on the following tick. Loader checks reject foreign-Console, unknown-channel and type-invalid bindings. This closes the shared-command read gap in the preceding receipt; messaging and the other migration requirements remain pending.

### Controller recipient and human messaging receipt

Coordination now registers Controller actors separately from physical Systems, using the full entity/Console/Controller address. Templates with `destination.kind: controller` specify both `console` and `id`. Human message actions carry Controller identity and operator generation through the existing command envelope; admission and execution both enforce current authority. Generated Console controls expose authorised templates. Delayed messages select the effective human operator at delivery, while addressed histories and pending identities survive handover and restoration.

Knowledge runtime saves containing Controllers use version 3 and preserve actor kind; the adapter verifies complete named bindings against the loaded layout. Earlier explicit-layout saves without these bindings are refused transactionally. Legacy knowledge saves without Controller actors remain readable. System inbox adapters cannot consume Controller-addressed messages.

The independent verification agent built and passed Controller runtime and gameplay knowledge integration suites, including duplicate Controller names across Consoles, physical-recipient impersonation refusal, delayed notification after AI-to-human handover, exact pending-message save/resume equality and obsolete queued sender rejection. AI tree send/receive/acknowledgement, factual reporters and public message presentation remain outstanding.

### Controller tree messaging receipt

Controller trees declare permitted message templates and use `send.<template>` through the same admitted template route as human actions. Addressed inbox metadata and typed `message_bindings` are frozen with other decision inputs. `acknowledge_message` advances only that Controller's cursor for an available addressed message. Message effects use the Controller's saved effect journal; outgoing interruption runs before operator authority transfers.

Independent native Controller and gameplay-knowledge suites pass. Integration checks cover one-shot AI sending, a human recipient yielding with unhandled history, payload-driven command execution, the actual acknowledgement cursor, exact continuation before and after acknowledgement, same-name recipient isolation and template/reader authority refusal. Factual reporters and mandatory public publication remain outstanding; this receipt does not establish the complete messaging step.

### Always-active factual reporter receipt

Physical Systems now run threshold reporters during telemetry production, independently of their Controller's operator. Reports carry physical source identity, measured arguments and Fact provenance with a distinct Reporter origin. They cannot bind AI memory, intents or decision observations. Warning debounce applies to the initial sample; healthy startup emits no recovery. Recovery delay and minimum intervals use persisted event latches. Restoration checks the reporter configuration and latch ownership before replacing live state.

Independent native Controller and gameplay-knowledge integration suites pass. Tests cover a human-operated System reporting to an AI Controller, factual metadata, warning/recovery timing, minimum gaps, unchanged-warning suppression across handover/restore and transactional refusal of missing reporter contracts. Reporter and Controller observation fields are preflighted against actual fitted telemetry before knowledge registration during `World.load`; unavailable fields are refused before play. Catalog-only validation still checks the global vocabulary rather than the complete fitted producer schema, so that compiler migration remains open. Mandatory public message publication and presentation also remain pending.

### Public Coordination delivery receipt

Entity-wide messages publish to the shared blackboard and permitted Console histories. Important AI-to-AI Controller messages also publish when the recipient is still AI at delivery. Public copies retain message identity and factual source metadata; spectator copies do not grant acknowledgement or action authority. Publishable schemas, payload fields and dynamic presentation bindings are validated to prevent private data entering public messages.

The independent verification agent passed native Controller and gameplay-knowledge integration suites covering public/shared visibility, private routing, delivery-time handover, human broadcasts, pending/delivered restoration and forged actionability refusal. This verifies publication and projection; rendered Viewscreen presentation remains outstanding.

### Fitted observation compilation receipt

Resource fitting compilation now belongs to content and is shared by validation and simulation. Per-System observation schemas derive from the compiled resource bindings and flight capabilities. Catalog loading rejects Controller/reporter fields absent from the fitted producer, before World construction. The vocabulary includes following, store forecasts and coolant forecasts/use with their actual types.

Independent verification passed native Controller, gameplay-knowledge, content and simulation integration suites. Synthetic fixtures compare complete compiled field sets with production telemetry for resource-bound engines, engines without stores and cooling equipment; unavailable fields and type mismatches are rejected. This closes the Catalog-only availability gap recorded above.

### Public Viewscreen and remote projection receipt

The Viewscreen now renders bounded shared-board/message readouts using the Console's localised evidence formatter and equivalent accessible text. Public projections deduplicate message identities, preserve evidence and omission counts, and cannot carry commands or actionable spectator copies. Local and network hosts expose the entity board without a Console claim. Approved remote clients receive the bounded public board through authenticated state transfers independently of station ownership; the direct-join fallback no longer attempts a nonexistent lobby-assigned claim.

Independent verification passed Controller runtime and transfer suites, compiled native presentation/host/client, and ran the real private-signalling lobby journey: an approved unclaimed remote client received a live publication with no private Console observation, then claimed a Console normally. Additional synthetic acceptance proves one shared throttle drives independently constrained resource consumers with conserved local reserves, and equivalent all-AI entities follow the same trajectory in and outside a player slot. Graphical/screen-reader smoke remains pending. A subsequent audit identified old reader-role restrictions on shared/public data; that cross-role visibility gap remains open until the next receipt.

The [Controller authoring guide](controller-content.md) documents the current explicit-layout contract and migration limits.

### Public scope and physical command contract receipt

Entity-board publication now means entity-wide readership: every Console/Controller can read published fields on that entity, regardless of private Console reader roles. Public message copies follow that scope without copying unreadable records into private Console boards or granting spectators action authority. Entity-wide templates always require public schemas/payloads. Targeted inboxes and private Console records retain their reader checks. Restoration validates public copies against the same publication contract.

Explicit command compilation uses compiled flight/resource capabilities and limits, including resource control kind, store-mode ownership and coolant capacity. Held-input ranges must include neutral zero; requests start idle. Independent native Controller, gameplay-knowledge, content and simulation suites pass, and both production packs verify. Synthetic tests cover a Controller without private reader roles acting on a shared record, private-binding refusal, cross-role broadcasts and restored spectator history, other-entity isolation, invalid physical controls/ranges and safe handover/request defaults. This closes the preceding cross-role visibility gap.

### Authored Controller widget receipt

Throttle and heading widgets bind directly to an owning Controller and command channel. They read accepted commands and compiled ranges from the same Console projection used by AI, and submit through current operator-generation admission. Shared and independent throttle widgets address their respective sections; client-safe assets preserve these bindings without exporting AI programs. Workshop binding controls author the same contract.

Independent verification passed native, Controller runtime, content and transfer builds/tests. Synthetic production-path journeys cover shared two-engine throttle, independent channels, heading, safe asset roundtrip, AI ownership refusal, malformed bindings, wrong actions and out-of-range numeric writes. Actual graphical touch/gamepad interaction remains part of the final manual gate.

### Flight guidance observation receipt

Match-speed and follow guidance now consume the owning Console's frozen permitted contact feed. The separate global body snapshot and actuator-specific sensor-range reconstruction are removed. Sensor changes take effect in the next observation snapshot, consistently with Controller decisions.

Independent Controller runtime, simulation and gameplay-flight suites pass. The real-path flight journey verifies matched speed against the tick-start Console contact velocity before target actuation; existing checks cover missing feeds, disabled radar, stale observations, restoration and reacquisition. Compound navigation commands and removal of legacy System AI remain pending.

### Compound navigation command receipt

Destination and follow settings now carry validated atomic tuples through Controller admission, commit, physical fan-out, projection, transport and continuation. Human and Controller-tree writes use the same path. Destination guidance is persistent physical work; a separate speed setting can adjust cruise speed without discarding its heading goal. Follow uses the permitted Console contact snapshot. The old System action adapter now calls common physical navigation admission rather than directly assigning flight fields, but that legacy decision/authority runtime still remains to be removed.

Independent native, Controller runtime, gameplay-flight, content and transfer suites pass. Synthetic checks cover human/AI movement parity, shared consumers, incomplete/foreign tuples, scalar extra-argument refusal, frozen accepted tuple reads, obsolete operator generations, exact resume, malformed-save transactional refusal and destination/cruise compatibility. Wire envelope 4 rejects correctly authenticated envelope-3 peers; its authentication vector was independently calculated. Flight save schema 4 adds destination continuation and reads schemas 2–3 with navigation inactive. Generated accessible argument controls and Workshop fields compile; graphical interaction and the complete remote continuation journey remain pending.

### Remote widget and reporter journey receipt

Network clients now admit arbitrary numeric Controller widget values only when the safe authored Console layout, current human section, generation, action and bounds match. Other Controller actions retain exact descriptor matching. A fresh reporter's empty latch history restores correctly during setup transfer; existing latch ownership validation remains enforced.

The independent real-signalling journey passed with ConsoleModel throttle fraction 0.333 producing 5661 on both physical engines, one targeted factual report, and remote complexity handover to AI. Invalid ranges, unknown addresses and post-handover human writes were refused. Native and lobby builds passed. This closes remote widget admission and initial reporter restoration; checkpoint/rejoin still requires the separate recovery journey.

### Evidence metadata and command receipt receipt

Controller record bindings now expose availability, source, observation/receipt ticks, uncertainty, evidence kind, withdrawal, staleness and revision from the same selected permitted record as the value. A withdrawn or stale stream cannot resurrect its older values. When no live field remains, metadata describes the newest matching unavailable stream. Owned command bindings expose accepted/consumed/pending revisions and request identity from the frozen command snapshot; human projections include the same consumption facts.

Independent Controller runtime, gameplay-knowledge and content suites pass. Integration journeys cover fresh/stale/withdrawn decisions, existing scope refusals, a physically refused command and next-tick receipt visibility, plus exact restored continuation. General contact selection bindings and the legacy runtime/content migration remain outstanding.

### Contact selection and latest record receipt

Controller trees can select the latest permitted record subject without reading world state, including explicit withdrawal metadata rather than falling back to an older objective. Contact bindings resolve a same-Console selector channel against the frozen permitted sensor feed and expose availability, motion, uncertainty and the producer observation tick. Workshop authors both bindings; transport preserves the observation timestamp.

Independent native build and Controller runtime, content, gameplay-knowledge and transfer suites pass. Checks cover tick-start selector isolation, missing and disabled feeds, foreign-Console refusal, latest-subject withdrawal, private record refusal, timestamp validation and exact continuation. Legacy control runtime and maintained-content migration remain outstanding.

### Shared preference channel receipt

Typed integer and boolean intent settings now use the same Controller admission, frozen reads, receipts and continuation as physical commands, with an explicitly empty consumer list. An AI Controller reads a human Controller's accepted preference through its permitted Console board and writes its own actuator channels. Physical commands still require compatible consumers and intent commands cannot name equipment.

Independent native, Controller runtime and content builds/tests pass. Synthetic journeys cover bounded integer/boolean preferences, generated controls, no pre-tick physical mutation, shared AI fan-out after commit, binding refusal, handover and exact continuation. This supplies the replacement primitive for legacy summary intents; removal of that old path and maintained-content migration remain pending.

### Initial command consumption receipt

Controller defaults now pass through the common physical consumer at spawn, before decision snapshots or AI evaluation. This prevents inactive sibling mode defaults from cancelling the first accepted destination/follow decision. Ordinary ticks still consume changed settings once. Neutral manual-axis releases are accepted in assisted mode without cancelling ongoing guidance; unsupported or nonzero manual input remains refused.

Independent native, Controller runtime, gameplay-flight and content builds/tests pass. Synthetic journeys cover first-tick human/AI navigation parity with inactive sibling defaults, accepted neutral receipts, guidance preservation across handover, malformed tuples and exact continuation. The maintained patrol smoke exposed the startup ordering fault; this receipt covers its source fix separately from content migration.

### Persistent cruise composition receipt

Physical command consumption now composes a changed destination with any accepted cruise-speed channel bound to the same mechanism. Composition runs after changed modes, respects later navigation cancellation and is independent of Controller/channel order. Destination-only mechanisms retain their default cruise behavior; follow retains its own speed limit. Reapplying the setting does not change command revisions or repeat discrete requests.

Independent native, Controller runtime and gameplay-flight builds/tests pass. Synthetic checks cover two-state AI destination changes with unchanged cruise, both channel orders, shared engines, human destination changes and exact continuation. The maintained patrol smoke now retains its authored cruise setting as objectives change.

### Maintained engineering and patrol migration receipt

The base courier engineering fit, cruiser, patrol leader and patrol follower now author explicit Console Controllers, profiles, owned command sections and permitted decision inputs. Repeated hulls share identical tree definitions. Assembly and resource composition remain authored; generator percentage-like decisions are converted to equivalent absolute output rates. Coarse orders use preference Controllers, thermal warnings use always-active physical reporters, and engineering requests address specific Controllers with important AI-to-AI public copies. Helm widgets address command channels. The simple survey/tug/traffic content still awaits migration with its existing vector motion mechanism preserved.

Both production packs verify. Independently triggered World smoke runs loaded the maintained advanced fits, executed 1200 ticks and compared 20 resumed ticks exactly. The patrol leader changed objectives while retaining cruise, the follower pursued its contact, and complete command receipts contained no refusals. These are manually triggered behavioral checks, not shipped tuning goldens. The smoke exposed the startup and cruise bugs fixed in the preceding receipts. Full legacy runtime removal, remote recovery and the final integration/GUI gate remain open.

### Explicit vector mechanism receipt

Explicit Console Controllers can now operate the existing vector engine and steering mechanisms without selecting inertial physics. Motion capability is independent of operator ownership; unsupported inertial commands and resource costs are rejected. Omitted heading defaults preserve spawn orientation, while explicit headings remain deliberate commands. Basic telemetry uses the common Console observation producer.

Independent native build and Controller runtime, content, simulation and flight suites pass. Synthetic journeys cover shared and split trajectory parity, fractional motion remainders, coasting and re-enable, timed AI/human parity, exact continuation, heading defaults and transactional bounds refusal. Legacy authority dispatch still remains to be removed.

### Maintained simple craft migration receipt

The survey, inherited tug and AI traffic now use explicit Console Controllers with vector motion, direct/automatic complexity profiles and addressed helm widgets. Traffic retains its timed patrol program on the navigation Controller. Engineering hulls explicitly retain inertial machinery. Heading channels preserve spawn orientation unless an initial heading is authored.

Both production packs verify. Independent manually triggered production World smoke checks operated survey/tug and unclaimed traffic for 450 ticks, confirmed vector/inertial model inheritance and spawn headings, and compared 20 restored ticks exactly. These are behavioral evidence, not production-content golden tests. All maintained controllable craft now author the new model; legacy runtime removal and the final recovery/integration gate remain pending.

### Legacy definition conversion boundary receipt

A separate compiler conversion produces explicit Controller layouts for unambiguous legacy ownership and timed policies, preserving vector/inertial capability, spawn heading, enabled state and resource setting defaults. Generated layouts pass the normal explicit compiler. System-context decision trees and summary-intent presets require explicit migration; ambiguous or out-of-range timed writes are refused. The source catalogue is unchanged.

Independent content build and integration suite pass, including single timed ownership, source immutability, physical defaults and diagnostic refusal cases. This helper is not yet the live runtime entry point; activation and removal of the old dispatch are the next step.

### Remote Controller continuation receipt

The real three-host signaling journey now uses explicit Controllers and preference channels. It operates a shared reserve throttle, changes complexity and Away state, receives a targeted factual report, and recovers through checkpoint/rejoin while preserving a consumed impulse request identity and refusing its obsolete operator envelope.

Independent native/session builds and the 25.5-second playable-fleet journey pass. The journey also verifies majority continuation, returning-member snapshot transfer, durable journal owner recovery, scoped scenario evidence and resource continuation. This closes the combined remote continuation acceptance gap; legacy dispatch removal and the final native/WASM/graphical audit remain pending.

### Engineering Controller interface receipt

Engineering cards now derive their controls from Controller channel subscriptions, show shared commands once, and use the Detailed complexity profile for manual operation. Allocation steps scale to the authored operating setting rather than the generic safety bound.

Independent native and Controller runtime builds/tests pass. The graphical engineering smoke admitted the Detailed profile and exercised generator, allocation and cooling controls across 18 cards; its fresh screenshot was inspected for readable layout and overflow. This is graphical smoke evidence, not screen-reader or six-display hardware acceptance.

### Unified runtime and compatibility receipt

The live World now converts unambiguous legacy definitions once at compilation and runs all command decisions through Console Controllers. Physical child entities no longer hold operator trees, human input state, summary intents or AI effect journals. Old per-System delegation and command dispatch are removed. Workshop inspection includes queued addressed Controller outputs.

World/session save schema 6 refuses old authority continuations and obsolete authority fields; wire envelope schema 5 refuses old peers before play. Independent native builds and Controller runtime, simulation, fleet setup, durable fleet recovery and authenticated wire/transfer suites pass, including an independent HMAC vector. Legacy flight/knowledge/preference fixtures are still being migrated, and the full native/WASM/GUI gate remains open.

### Preference, lifecycle and observation-only Console receipt

Controller preference channels now support bounded localised choices enforced for human/AI admission and restored state. Scoped publication activation/stop and request withdrawal preserve cleanup on task interruption without granting Controllers a fact-writing API. Private last-action results persist with continuation. Console human presence is updated independently of Controller count, so observation-only seats receive human-seeking offers across claim/Away/recovery.

World load executes a freshly prepared catalogue rather than stale cached programs. Workshop authoring uses Controller profiles and scoped Controller binding palettes; the old per-System policy/preset affordances are removed.

Independent native builds and Controller runtime, paid flight, knowledge/Coordination, preferences/Workshop dependency roundtrip and wire suites pass. Coverage retains typed nested message capture, delayed delivery, interruption cleanup, private evidence/staleness, non-choice refusal, mutable-catalogue execution, request identities and exact continuation. The final cross-platform and graphical gate remains pending.

### Final identity and graphical workflow receipt

Commit `c5b2218` removes the last physical-System AI memory/inbox registrations and unused gameplay inbox adapters. Explicit layouts reject System message destinations with an actionable Controller migration diagnostic. Independent knowledge, content and Controller runtime suites pass, including forged physical-AI continuation refusal. Restore retains public/addressed history and clears transient notifications so reconnect does not replay popups.

Commit `440e840` fixes separate throttle geometry and makes the graphical probe verify every bound command and physical consumer, with distinct synthetic requests. It also reconciles the renderer with Workshop's normalized `gameplay_panel` coordinates, respects explicit panel layout and bounds scrolling reports. The obsolete entity AI picker is removed; authoring directs users to Console Controllers.

Explicitly triggered native raylib/OpenGL runs at 1280 × 720 passed with fresh screenshots inspected:

- Shared throttle drives both synthetic engines at 12750; separate throttles drive 12750 and 4250. Settings Away/resume uses the ordinary Controller handover path.
- Lobby selection changes to the alternate complexity profile, verifies the authoritative selection, restores the initial profile, and completes Ready/countdown/Settings/save/quit. A separate Console Settings journey selects automatic/direct through visible controls and verifies profile and effective operators across Away/return.
- A physical reporter delivers factual requested-speed evidence to the addressed Controller while a human operates it. Console text is readable within the scrolling panel. The public report appears on the Viewscreen; the ordinary addressed report does not.
- Workshop's explicit test-only probe edits an initial command to 3000, verifies/exports/reopens it, and activates production Test Start/Pause/Step/Restart handlers. The live command editor and consumed output are visible; Step advances exactly one tick. This is app-driven UI workflow evidence, not physical mouse editing or accessibility certification.

Reproduction commands are in [Controller content](controller-content.md#compatibility-and-verification). Local diagnostic captures are retained under `build/controller-ui-shared4.png`, `build/controller-ui-split4.png`, `build/controller-report-console4.png`, `build/controller-viewscreen4.png` and `build/controller-workshop-smoke.png`; generated captures are not source fixtures or tuning goldens. The menu run uses the existing explicit `--menu-smoke` journey. The independent seven-step source audit found no further functional gaps; the full integration gate remains the final completion condition.

### Broader integration migration receipt

The first full native gate found two older expectations inconsistent with the agreed model. Commit `4c75acd` updates the production-path integration journeys: every same-entity reader sees deliberately published fields while private Console data and other entities remain isolated; addressed Controller deliveries cannot be consumed by a neighbouring Controller, while Console-wide deliveries retain independent cursors. Portable recovery explicitly requires fresh human operator generations and compares every other saved field exactly after continued execution. Both previously failing suites pass independently. The same commit adds the Console Settings profile-selection smoke described above. No runtime permissions or continuation checks were relaxed to obtain those passes.

### Remote admission and cross-platform lifetime corrections

Commit `5eb2f78` migrates the real live-session probe to current publication and Controller message descriptors, binding the permitted entity target as Console presentation does. It checks obsolete System envelopes, unknown templates and forged operator generations are refused. The complete real service gate passes all eight cases, including three-host checkpoint/rejoin, majority recovery, live session operation, verified WSS and pregame lobby flow.

The first full WASM run exposed a C++20 lifetime bug in initial reporter validation: a range loop dereferenced a temporary equipment optional that expired before iteration. Commit `9b720d9` retains the optional in the loop initializer. Native simulation and Controller suites and the previously failing release-config WASM simulation suite pass. No memory limit increase or diagnostic-only build flags are part of the fix. An audit found no other instance of this temporary-optional range pattern in the changed runtime. The complete gate is rerun against this correction before closing delivery.

### Final completion and gap audit

All seven steps are implemented and verified. The independent verification agent audited the final runtime against the original plan, including common human/AI command admission, shared and separate physical subscriptions, observation boundaries, factual reporting, Controller routing, complexity handover, generic entity autonomy, authoring and recovery. The gaps found during integration and graphical verification are closed in the receipts above. No old System command-tree dispatch, individual delegation controls, direct AI physical mutation or duplicated physical parent authority remains. Equipment ordinal lookups are derived from Flecs children.

The complete `scripts/check.ps1 -Target all` gate passes after the lifetime correction: 21 native integration tests, eight real service cases, 18 configured WASM checker programs, 11 HTML launcher tests, native build-check and identical native/WASM controlled continuation traces. The WASM gate now explicitly includes the Controller runtime suite. Both maintained production packs verify together (10 entity definitions, six maps, seven Consoles). Local evidence is in `build/controller-full-gate-final.log` and `build/controller-production-verification-final.log`.

The GDD, architecture, content, knowledge, tree, network, presentation and delivery documents now describe the implemented Controller model and explicit compatibility boundary: World/session save schema 6 and wire schema 5 refuse old authority continuations and peers. Repository formatting and documentation link checks pass. The native graphical workflows recorded above cover the requested lobby/settings, throttle, handover, reporting, Viewscreen and Workshop journeys. Screen-reader usability, simultaneous physical devices and six-display performance remain separate hardware acceptance work, as scoped by this plan; this completion does not claim those runs.
