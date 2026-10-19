# Gameplay Workshop authoring

Draft authoring choices and previews are resolved by `WorkshopDocument` against the current draft and loaded dependencies. Local definitions take precedence for previews; verified export still requires explicit override declarations. Knowledge previews include the draft's `history_limit`, using the same merge rules as production pack loading.

Controller vocabulary queries resolve the selected tree's inheritance and required schemas, so an unrelated incomplete tree or schema does not disable its palette. Required missing, duplicate or invalid definitions produce contextual errors. Queries do not mutate or cache the editable draft; changes, undo/redo and dependency imports are visible immediately. These previews do not establish that the whole draft can be exported.

Workshop has one active selection and one **Properties** inspector. Select an entity in Definitions or on the Map to edit appearance/physics, fitted Systems and ownership, power/cooling, assembly placements, knowledge feeds and Console complexity profiles. Select a System within the fitted Systems section to edit its capabilities and template. Console selection opens its layout, bindings, knowledge roles and preview in the same inspector. Reusable templates/assemblies, knowledge schemas and Coordination templates are selected from Definitions. All edits use the ordinary Workshop draft and production verification/export paths. Opening inspectors does not activate equipment, publish information, spawn actors or modify a running game.

For the new explicit Controller model, see [Controller authoring](controller-content.md). Entity Properties now edits Console Controllers, owned commands, physical subscriptions and complexity profiles; tree Properties edits Controller bindings using the resolved production vocabulary. New authoring uses Controller/scenario trees and Console profiles. Imported legacy policies require explicit migration when the boundary converter cannot establish unambiguous ownership; Workshop no longer offers individual System policy or delegation editors.

## Authoring areas

| Area                     | Authored data                                                                                                                                                                                           |
| ------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Systems and ownership    | Stable fitted identities, capability parameters, physical observation ownership, Station/Console layout and read feeds, explicit widget instance targets and normalized gameplay-panel placement        |
| Templates and assemblies | Reusable equipment, assembly internal Systems, typed power/coolant/information service endpoints, repeated placements, explicit external bindings and nested local overrides                            |
| Power and cooling        | Stores and local reserves, generators, groups, finite conduit links, thermal protection/overdrive parameters, coolant stocks and cooling links                                                          |
| Knowledge                | Catalogue schemas and typed fields, producer/reader roles, Console reader/producer roles, per-System telemetry records, explicit Console feeds and publication templates                                |
| Coordination             | Reusable typed message templates, explicit destinations, human-seeking offers, semantic field bindings, translated presentation parameters, urgency/delay/expiry/request keys                           |
| State trees              | Controller/scenario context, hierarchical state graph, ordered tasks/transitions, typed record/command/telemetry observations, parameters, private memory, references and named inherited-state patches |
| Scenario                 | Regions, tree/parameters, reusable situations, spawn points/groups, reach-region definitions, actor/objective/region/event and recipient bindings, publications and bounded runtime limits              |
| Console Controllers      | Console Controllers, owned command sections, physical subscriptions, labelled preferences and complexity allocations                                                                                    |
| Test                     | Explicit mission, ship and slot selection; disposable production test requests and read-only runtime feedback                                                                                           |

Identifiers entered by the author are displayed verbatim. Tool labels and explanatory text use shared translation/pseudo-localisation. The tool keeps stable ImGui identities and keyboard-compatible controls. Player semantic controls are authored and verified through the existing Console model; this tool does not create a separate player model.

Assembly internal Systems use the shared capability/mechanism inspector and can override capability and endpoint defaults without writing absent inherited values merely by viewing them. Coolant sources expose an optional pump power store, including `$service` bindings; leaving it absent preserves a passive source. Production verification checks that a paid pump has its dedicated positive-rate Coolant consumer.

## Trees and inheritance

For patrols, author reach-region objectives under Scenario, then add an `objective_sequences` entry with their ordered IDs and enable `repeat`. Start it once with `start_objective_sequence`. Its saved current leg and cycle advance in the production scenario runtime, without repeatedly adding objectives or situations.

Controller trees write owned command bindings. A `follow_contact` channel carries active state, contact identity, separation and speed limit, and may subscribe compatible engine/steering mechanisms to the same command. It reads only the owning Console's available contact feed; missing evidence requests braking and holds heading until reacquisition. Power and heat constrain actual motion. Destination and other compound commands use the same typed binding editor and admission. See [the command grammar](controller-content.md).

The separate **State trees** area selects a tree and displays its nodes and transition edges; the selected tree/state is edited in **Properties**. **Scenario** selects the same map used by Map and opens its scenario fields in Properties. **Test** remains a separate disposable runtime area; its mission/ship/slot choices do not change the authoring selection. The node list uses parent depth for layout; selecting a node exposes its ordered tasks and conditions. Move controls change actual authored array order. The production vocabulary supplies action names, context permissions, expected argument names/types and observation/intent options. The editor does not maintain an independent executable action vocabulary.

Inherited trees are displayed through a read-only resolved view. Base context, states and parameters remain inherited until an explicit local edit or named-state override is requested. Reference calls retain their named parameter bindings. Literal parameter defaults, literal argument overrides, parent-parameter call bindings and ordinary runtime bindings use separate permitted source choices. Legacy timed trees retain their state names as object keys and their normal numeric/boolean output fields.

Named record bindings retain inherited slots. Controller command, telemetry, contact, message and publication binding groups display their resolved inherited values read-only until explicitly overridden as complete groups; removing a local group restores inheritance. Private memory remains distinct from accepted command intent and producer observations. Authored preference choices carry numeric values and localised labels, and are validated through the production compiler.

The graph highlights active state names supplied by the disposable runtime. The Test feedback tree includes transition reasons, admitted pending outputs, actual resource grants and telemetry, provenance records and pending messages. Commands disappear from the pending list when their tick executes. Console Controller and scenario inspection have independent bounds, so a large ship cannot hide every scenario controller.

## Draft history and verification

**Edit > Undo/Redo** applies across Workshop, including entity and Console edits, maps/scenarios, state trees, pack metadata, imported assets and dependencies. Ctrl+Z undoes and Ctrl+Y or Ctrl+Shift+Z redoes; active text fields retain their own text-editing shortcuts. Continuous drags/text edits become a document checkpoint when the interaction finishes. New/open pack starts a fresh history; saving does not clear history. Snapshots share unchanged asset/dependency storage. Each history keeps up to 128 snapshots within a 32 MiB additional serialized-data budget (excluding the latest resource set), retaining at least two snapshots so a large asset does not disable Undo. A new edit after Undo discards Redo.

The single Workshop **Verify** menu action calls `WorkshopDocument::verify()` for the complete draft/dependency closure and reports the production diagnostic. Export and Test Start also require verification. Incomplete drafts remain editable; undo/redo does not require a draft to be valid.

File → Export pack copy downloads a verified ZIP in browsers, including embedded browsers without a working native save picker. It does not change the current save destination. Save continues to update an installed pack; Save as selects a separate destination.

Renaming an entity repairs static placements, allowed ships, entity inheritance, scenario spawn-group placements and literal `spawn_entity` definition arguments in state tasks and inherited-state patches. It preserves ordinary text, result handles and runtime bindings even when their text matches the previous definition ID.

## Disposable test bridge

The tools library has no simulation or Flecs dependency. Its public interface is:

```cpp
void draw_workshop_areas(WorkshopDocument &, Application &, std::string &status, std::string &selection);
void draw_gameplay_properties(WorkshopDocument &, Application &, std::string &status,
                              const std::string &selection, const std::string &entity);
void set_gameplay_authoring_vocabulary(const Json &metadata);
void set_gameplay_test_feedback(const Json &feedback);
std::optional<GameplayTestRequest> take_gameplay_test_request();
```

`set_gameplay_authoring_vocabulary` accepts the production `gameplay_tree_vocabulary()` metadata directly, including context masks and typed argument signatures. It also accepts an already partitioned `{system:{actions,observations,intents},scenario:{...}}` view with stable descriptor `id` values.

The authoring controls initialise metadata from the production definition library. Controller task palettes are compiled from the resolved tree's bindings, including scoped message/publication actions and owned command writes. Dependency schemas, templates and expanded assembly identities participate in binding choices. Imported invalid values remain visible and fail verification rather than silently changing on inspection.

`GameplayTestRequest` contains verified pack bytes, exact dependency archives, selected `mission`, `slot`, `ship`, and a `GameplayTestAction` of Start, Pause, Step, Restart or Stop. Start captures the current draft only after verified export succeeds. The native/server application consumes requests and owns the disposable production world. Pause/Step/Restart/Stop carry no draft replacement or live-session authority. The host reports failures through the ordinary status and feedback paths.

Feedback is read-only structured data: `running`, `paused`, `tick`, `active_states`, `transition_reason`, `diagnostics`, `console_records`, `ship_records`, `pending_messages`, `pending_outputs` and `resource_grants`. The host reports at most 32 Console Controllers and 32 scenario controllers, 64 resource grant rows, 32 pending outputs and 32 pending messages; `truncated` counts omitted entries. Each grant includes actual paid/requested energy, funded output fraction, overdrive and the bound System's resource telemetry. Private AI memory is not exported. Controllers include source tree identities so repeated instances remain distinguishable.

## Verification evidence

Source review covers schema consistency, inheritance editing, preservation of optional inherited values, scoped ImGui cleanup, and absence of simulation imports. Build/link checks and actual graphical Workshop journeys are separate evidence recorded in the gameplay implementation ledger. A successful source review does not establish playable, screen-reader or physical-device acceptance.

The disposable host is implemented by `phoenix.workshop_test` in `src/simulation/workshop_test.cppm/.cpp`. It performs transactional production pack/dependency resolution and slot/ship validation, owns a separate `World`, and advances at the normal 20 Hz. Pause toggles pause/resume; Step pauses and executes exactly one tick; Restart reconstructs the captured snapshot; Stop releases that world. A failed replacement preserves the previous disposable instance and reports its diagnostic.

Native integration imports `phoenix.workshop_test`, includes `src/entry/workshop_bridge.hpp` after imports, and calls `pump_native_workshop_test(app, elapsed_seconds)` once per frame. Browser Workshop calls `pump_browser_workshop_test(app)` from the tools module. It transfers exact archive buffers to `workshop-test-worker.js`; the worker loads a separate headless C++/WASM program named `phoenix_workshop_test.js`. The ordinary Workshop binary retains its existing dependency boundary and never links simulation/Flecs.

The worker build uses `src/entry/workshop_test.cpp`, modular factory `createPhoenixWorkshopTestModule`, worker environment, memory growth, `EXIT_RUNTIME=0`, exported `ccall` and `HEAPU8`, plus `_malloc`/`_free`. Its KEEPALIVE endpoints accept bounded archive uploads, typed JSON requests, elapsed time and read-only feedback. Archive bytes enter through a heap-allocated scratch buffer of at most 64 KiB, passed to `ccall` as a numeric pointer; uploads never allocate archive chunks on the Emscripten call stack. The worker rereads the exported heap view after memory growth. Each archive is at most 32 MiB, with at most 64 dependencies and 128 MiB total transfer. The worker and its generated `.js`/`.wasm` sit beside the browser role scripts.

`tests/system/workshop_test.cpp` covers isolated host progression, pause/resume, one-tick stepping, transactional refusal of an invalid replacement, restart from the original snapshot and disposal through actual production packs. Browser worker loading and graphical interaction remain manually triggered acceptance evidence.

Browser test requests are acknowledged and bounded to 16 pending operations and 128 MiB of queued archive transfers. A failed worker/script/WASM load reports its diagnostic and permits a later Start retry without reloading Workshop. Worker failures remain visible across heartbeat feedback until a subsequent request succeeds; request responses include `request_succeeded` so a rejected request cannot clear the error. Root `World::debug()` must provide bounded diagnostic lists before 10 Hz worker serialization.
