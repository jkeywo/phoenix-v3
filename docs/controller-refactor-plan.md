# Console Controller refactoring plan

Approved target · 19 October 2026. **Status: planned; no runtime migration completed by this document.** Implement the [agreed architecture](controller-architecture.md) and revised [GDD](GDD.md), preserving existing physical capabilities and deterministic execution. Historical gameplay completion receipts describe the earlier architecture.

## Current code and required changes

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

| Step                                                         | Status      |
| ------------------------------------------------------------ | ----------- |
| Agreed architecture, GDD reconciliation and refactoring plan | Documented  |
| 1–7: implementation and acceptance above                     | Not started |

The existing gameplay ledger remains evidence for the previous runtime. It does not establish completion of this refactor.
