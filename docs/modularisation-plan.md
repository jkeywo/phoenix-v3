# Modularisation implementation plan

Delivery plan, 27 October 2026; execution began 27 October at 23:12 UTC. No migration chunks are complete merely because this document exists. The [module ownership reference](modules.md) describes the current implementation; update it as each chunk lands. The [GDD](GDD.md), [technical stack](technical-stack.md) and [testing policy](testing.md) remain authoritative for behaviour and validation.

Current execution evidence is recorded in [modularisation-evidence.md](modularisation-evidence.md). Statuses below distinguish active source extraction from completed acceptance: browser, full native integration and final dependency enforcement remain gates. The current execution starts at revision `34883ac` with save schema 32; the older inspection inventory below is historical.

## Outcome and constraints

Organise first-party C++ into `src/<family>/<module>/`, with explicit ownership, independently compiled implementations and enforced dependency seams. A maintainer should be able to find a concept by its family, read its interface, and understand its implementation without reconstructing a giant enclosing class.

This is a structural migration. Preserve gameplay, authored formats, command/knowledge restrictions, deterministic ordering, platform behaviour, worker ownership, save/restore and public browser callbacks. Do not combine it with tuning, new gameplay, dependency upgrades, replacement UI frameworks or completion of deferred gameplay-engine features. Existing saves at the starting revision must remain usable; moving declarations does not justify a schema bump.

The inspection baseline is `640e1e2`, with 124,881 nonblank lines under `src/`, including comments and interfaces. The earlier conversation's 109,889-line estimate predates rules, custom properties, machinery processes and finite work. Recount before starting; sizes guide reading and scheduling, not acceptance thresholds. Current effects migration evidence includes schema 31; use the actual starting revision's formats if development has advanced.

## Folder and module conventions

- Every first-party C++ source, interface and private header ends in a module folder below a family folder. No implementation files directly under `src/` or a family folder. Do not create empty module folders in advance.
- A family groups related modules; it is not a compulsory umbrella import or static library. A module folder has one coherent owner and may contain several implementation files. Domain widget/editor families may use child implementation directories if useful, but they must not become another unbounded aggregate.
- Preserve existing named C++ module names when their interface remains coherent. New names use `phoenix.<family>.<module>` with underscore identifiers where paths contain hyphens. Keep exactly one declaration owner; update imports rather than introduce compatibility headers or duplicate declarations.
- Use public `.cppm` interfaces for interfaces used by another module. Keep private declarations and helpers inside the owning module. Independently compiled `.cpp` units replace class-body include fragments. Do not include another module's private headers.
- Each module gets a short `README.md`: purpose, interface entry point, state owner/lifetime, allowed dependencies, execution/restore obligations, and relevant system cases. Family READMEs provide the reading order and dependency map. Prefer these concise guides over comments repeating code.
- Use one CMake target per independently enforced module seam. Closely related implementation units can share that target. Preserve distinct definition/runtime targets where runtime code must be excluded from clients and tools. Family convenience targets, if needed, are `INTERFACE` aggregations and are not used to hide broad dependencies.
- Retain existing executable names, output filenames, script entry points and deployment paths. Register sources explicitly; do not replace source lists with recursive globbing. Mirror ownership in CMake links and C++ imports.
- `web/`, `service/`, `tests/`, `content/`, `scripts/`, `cmake/` and third-party assets remain their existing top-level projects. Browser JS adapters may remain under `web/`; the C++ folder rule does not require moving the launcher or service. Synthetic tests retain their current identities and fixtures; do not reorganise all tests merely to mirror production folders.

### Destination hierarchy

Each indented leaf below is a module folder, not an individual file. The list defines the destination for the whole first-party C++ tree.

```text
src/
  foundation/
    data/                 # JSON/bytes, hashes and portable data helpers
    contracts/            # common IDs, commands, observations and scene records
    input/                # logical Console regions, routing and release rules
    console-controls/     # renderer/session-free command eligibility
  behavior/
    state-tree/           # evaluator, compiler and private instance memory
    vocabulary/           # shared gameplay vocabulary, no World execution
  content/
    packs/                # archive encoding, immutable pack identity
    catalog/              # catalogue data, dependency/override resolution
    verification/         # cross-domain preparation and verification orchestration
    equipment/            # fitting and physical service definitions
    combat/               # combat and directed-capability definitions
    operations/           # personnel, teams, away and boarding definitions
    logistics/            # craft, docking, transport and stock placement definitions
    sensing/              # sensors, psychic apparatus and interdiction definitions
    controllers/          # Console/Controller profiles, actions and bindings
    knowledge/            # authored knowledge and tree observation bindings
    effects/              # effect, ability, rule, property and process compilers
    resources/            # catalogue-specific resource/recipe/binding compilation
    scenarios/            # scenario and conversation catalogue integration
    ui/                   # layouts, themes, availability and widget definitions
    assets/               # image/model/sprite/audio compilation and distribution
    documents/            # WorkshopDocument, drafts, history and draft resolution
  resources/
    stocks/               # stock definitions, selections and transactions
    networks/             # native power/coolant definitions and allocation
    processors/           # generic recipe definitions and processor accounting
    equipment/            # authoritative fitted-System resource adapter
  knowledge/
    records/              # knowledge definitions, provenance, retention, messaging
    consoles/             # authoritative Console/ship knowledge integration
    projections/          # permitted observation assembly and immutable snapshots
  control/
    ownership/            # claims, handover, generations and command admission
    controllers/          # human/AI command sections and compiled bindings
    execution/            # tree action execution through supported domain ports
    coordination/         # addressed messages, stances and navigation intent
    providers/            # Controller proposal integration and fallback policy
  effects/
    contracts/            # invocation, owner, work and native outcome records
    properties/           # modifier contributions and custom properties
    transactions/         # preparation, payments, validation and atomic commit
    lifecycle/            # grants, expiry, pending queue, receipts and payloads
    rules/                # expressions, source binding, triggers and maintained rules
    abilities/            # preparation, exclusions, activation and recovery
    processes/            # machinery/finite lifecycle and work credits/receipts
  gameplay/
    fitting/              # fitted equipment instances, capability and binding indexes
    flight/               # native propulsion, steering and navigation assessment
    combat/               # weapons, ordnance, shields, damage and destruction
    sensors/              # native detection, focus and environmental reach
    psychic/              # cloak, ghosts, wards and native reception/projection
    personnel/            # officers, duties, conditions and clinical outcomes
    teams/                # team orders, routes, repair and Medical work
    away/                 # away parties, transport choice and mission work
    boarding/             # encounters, capture and ownership consequences
    conversations/        # conversation compiler/runtime and World adapter
    docking/              # connections, guidance and physical attachment
    craft/                # launch, orders, recovery and servicing
    logistics/            # cargo, deliveries, replenishment and supply offers
    ftl/                  # physical reserve, recall, departure and reports
  scenarios/
    runtime/              # scenario definitions and situation execution
    environment/          # regions, fields and asteroid generation
    objectives/           # objectives, priorities and debrief evaluation
    integration/          # typed World/scenario operations and publications
  world/
    state/                # minimal authoritative ECS identity/body/state contracts
    spatial/              # spatial indexes and collision infrastructure
    lifecycle/            # compiled world assembly, spawn/remove and World facade
    schedule/             # explicit phase orchestration and tick profiling
    persistence/          # top-level save/restore envelope and restore ordering
  networking/
    session-api/          # shared SessionPort and role/setup contracts
    local-session/        # local execution, frames and worker session ownership
    remote-session/       # host/client adapters and permitted remote projections
    lobby/                # shared protocol and authoritative admission composition
    wire/                 # packets and bounded transfers
    consensus/            # simulation-independent consensus actor
    fleet/                # fixed-roster peer driver and host fleet composition
    durability/           # journal recovery and durable acknowledgement coordinator
    inference/            # external decision-provider protocol/client
  graphics/
    assets/               # GPU textures and graphics resource lifetime
    scene/                # shared renderer; private sprite/model implementations
    radar/                # radar primitives, icons and sweep rendering
    audio/                # music, audible events and presentation sound resources
  ui/
    console-model/        # renderer-free ConsoleModel and action/presentation helpers
    console-surface/      # per-Console composition, caches and interaction ownership
    flight-widgets/       # Helm and navigation controls
    tactical-widgets/     # targeting, sensors, combat and abilities
    engineering-widgets/  # machinery, resources, faults and repair presentation
    operations-widgets/   # personnel, teams, away, craft and logistics presentation
    information-widgets/  # Comms, knowledge, objectives and reports
    runtime/              # RmlUi contexts, fonts, backend and resource registration
    interaction/          # shared focus, pad navigation and semantic model
    screens/              # menu, lobby, settings and loading presentation
    viewscreen/           # authorised scene composition, sidebar and cameras
    preview/              # authoring-only sample model and Console preview
  workshop/
    shell/                # ImGui integration, docking, selection and common controls
    workflow/             # document open/save/import/export and history actions
    map-editor/           # map, slots, entity placement and gestures
    layout-editor/        # Console layouts and widget composition
    gameplay-editors/     # domain inspectors in separate implementation files
    tree-editor/          # state-tree and Controller authoring
    preview/              # material/scene previews and presentation configuration
    test-client/          # isolated native/browser test requests and feedback
    gm/                   # live permitted session inspection
  application/
    shell/                # screen/session transitions and composition state
    library/              # installed packs, menu data and content loading
    preferences/          # localisation and personal/tool presentation preferences
    connections/          # host/join/reconnect flow through session interfaces
    displays/             # monitor regions and stable device assignments
  platform/
    devices/              # native/browser device adapters
    files/                # file dialogs, import/export/download and clipboard
    storage/              # persistent blobs/journals and atomic file adapters
    transport/            # socket/WebRTC adapters and shared transport policy
    accessibility/        # Windows UIA, browser DOM/ARIA and Linux adapter
    lifecycle/            # platform focus, browser host callbacks and main-loop glue
  executables/
    native/               # native composition and startup
    browser/              # server/client/Workshop role composition and exports
    pack/                 # command-line packager
    verify/               # command-line verifier
    workshop-host/        # disposable authoritative test host, separate executable
    inference-bridge/     # external provider bridge executable
    diagnostics/          # build checks and explicit profiling support
```

Do not create a separate native or web gameplay/UI implementation. OS/browser variants live beside their module's adapter interface, for example `platform/files/windows.cpp`, `linux.cpp` and `browser.cpp`.

## Ownership and dependency rules

### Data and definition flow

`foundation` is below domain definitions and has no graphics, simulation, application or OS UI dependency. Keep the existing minimal secure-randomness and atomic/byte-file implementations private to `foundation/data`, including their native/browser variants. They support its existing portable contract and must not introduce a reverse dependency on the higher-level platform family. User dialogs, downloads, clipboard, blob storage and journals remain platform services; foundation is not a general OS utility collection.

Keep generic definitions with their domain modules: state-tree programs in `behavior/state-tree`, knowledge records in `knowledge/records`, stock/network/recipe definitions in their resource modules, and scenario definitions in `scenarios/runtime`. Their catalogue-specific JSON resolution belongs in the corresponding `content` module. Preserve definition-only target splits so compiling/validating authored data never links authoritative execution accidentally.

`content/catalog` owns the catalogue model and generic resolution, not every compiled definition. Domain compilers import that model and only the definition contracts they need. `content/verification` assembles compiler results and cross-domain validation above them. Compilers must not import the verification orchestrator. `content/documents` uses compilers and verification without importing graphics, tools or authoritative runtime.

Split shared declaration dependencies before splitting implementation: move simple referenced types to their real owner or a small lower-level contract module, then redirect importers. Do not solve cycles with duplicate types, JSON round-tripping, backdoor private headers or moving all types into foundation. Keep normal JSON at the established authoring/wire/save seams; use typed runtime operations for new internal seams.

### Authoritative execution

- `world/lifecycle` assembles the World facade and subsystem instances; `world/schedule` retains one explicit tick sequence. Neither implements equipment algorithms, Controller decisions, effects or domain projections.
- `world/state` provides minimal shared ECS identity/body contracts and lifecycle access. Domain components, queries, caches and mutable continuation belong to their owning modules. It must not become a replacement `World::Impl` exposed to everybody.
- Domain runtime modules may use Flecs internally. Flecs never leaks into client/tool/shared session interfaces. Every cached query is destroyed before its World; subsystem teardown precedes ECS destruction.
- Cross-domain operations use narrow typed ports defined by the consumer's needed contract, connected in World composition. Use an ordinary explicit collaborator interface/callback as appropriate; do not build a general service locator, event bus or plugin framework.
- Prevent cycles such as combat -> effects -> combat: effect transactions depend on native outcome ports/contracts; combat implements its native operations, and World wires them. The same applies to resource payments, clinical outcomes, machinery work and ability-granted processes. Physical damage, patient/fault progress, power allocation, bay deliveries and FTL reserve state remain native-owned.
- Effects submodules share one execution/transaction path and stable invocation identities. Preserve exact work receipts, fractional credits, pre-generation funding, independent reaction failure and exactly-once completion. No second scheduler, wallet, damage algorithm or mutable completion flag is introduced.
- Controllers consume permitted observations and issue admitted intent. Equipment executes commands. Knowledge records retain provenance and scope. The tree evaluator does not acquire World knowledge or physical actuation.
- `world/persistence` owns the envelope and ordered restore orchestration. Each subsystem owns encoding/validation of its continuation section. Keep existing field names, ordering where contractual, stable IDs, revision checks and restore reconstruction; transient compiled bindings/caches remain rebuilt rather than saved.
- Retain the existing simulation worker, synchronization and snapshot lifetimes. Submodules are not new threads. Rendering frequency never changes simulation order or authoritative work.

### Presentation, tools and host composition

`networking/session-api` is the route from shared application/UI to local or remote sessions. Console eligibility remains foundation-level; authoritative admission remains server-side. `ui/console-model` owns one route's permitted state and pending intent, without renderer or device ownership.

Widget modules use the Console model, layout and shared interaction contracts. They do not own an `Application`, unrestricted World, authoritative subsystem or another widget's private state. Surface lifecycle coordinates focus/capture and invalidation. Share the same permitted model and stable identity for visual and accessible controls.

Extract semantic/control placement records from nested `Application` types into `ui/interaction` contracts before making platform accessibility depend on them. `Application` aggregates owned groups with explicit lifetimes; it is not passed everywhere as a mutable registry. Shared translation resources/preferences stay independent of both UI frameworks.

`graphics` consumes authorised scene/audio projections and assets. Runtime and Workshop use the same renderer. GPU resources are released on the owning render thread before window/context teardown and invalidated on content replacement. Platform accessibility consumes semantic snapshots and queues supported actions; it does not read simulation internals.

Workshop edits `content/documents`. GM consumes live session inspection. They share ImGui infrastructure and rendering, not mutable document/session ownership. Disposable testing uses a separate host; browser Workshop never links that host or simulation transitively.

Keep low-level networking seams acyclic: transport/wire/consensus below host/fleet composition; session API below session implementations. External inference protocol belongs to networking; Controller fallback/decision policy belongs to control. Platform storage adapters implement storage contracts; durable consensus coordinates persistence without owning platform files.

### Enforced seams

Extend `cmake/Boundaries.cmake` rather than rely only on folder names. Give each new target an allowed direct dependency set and validate the full resolved graph, including configuration-dependent links. Retain and extend transitive guards against:

- simulation, Flecs, host/local execution or fleet setup in browser client and Workshop UI;
- graphics, RmlUi, ImGui or tool implementations in authoritative simulation;
- application/UI/World in generic tree, stock, knowledge, consensus and portable contract modules;
- domain compilers importing catalogue verification or Workshop UI;
- module-private headers imported across module folders or class-body implementation fragments remaining in completed modules.

Add a lightweight source/import ownership check to the normal non-graphical gate. Account explicitly for private module partitions and standard/third-party global fragments. Check the graph and ownership rules, not line counts or exact historical source lists. Prove the checker rejects a forbidden edge using an isolated generated configure fixture; do not add a separate test per module.

## Migration chunks

Execute in the numbered order below. Each row is a separately finishable chunk; a chunk may contain several verified commits. Do not open all folders or rename all module declarations up front. Move the relevant source, update build/import/path consumers, extract its implementation and remove its old path within that chunk.

While a later subsystem is still embedded in World, an explicitly tracked temporary include fragment is allowed. It is not a finished module. A completed extraction must compile independently and no longer need enclosing class state. If a temporary umbrella import is needed, record its remaining consumers and remove it in chunk 31; do not duplicate runtime paths.

| ID  | State                                    | Work and source-to-destination allocation                                                                                                                                                                                                                                                  | Completion focus                                                                                         |
| --- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------- |
| 00  | Baseline recorded                        | Capture starting revision/status; inventory every first-party source, module declaration, target, entry point and cross-module private include. Record the phase sequence, save schemas, browser exports and baseline checks. Create the source ownership ledger described below.          | Every file has one destination; baseline failures are identified before edits.                           |
| 01  | Active; gates pending                    | Move `core/data*`, `contracts*`, `input*`, `console_controls*` into the four foundation folders; move the existing `platform/random.cpp` and `atomic_file.cpp` data-module implementations beside their owner; move `behavior/*` into state-tree/vocabulary.                               | Existing import names and definition/runtime isolation survive both compilers.                           |
| 02  | Native content pass; integration pending | Extract pack/catalog model and archive operations from `content.cppm`, `pack.cpp`, `catalog.cpp`. Create `content/verification` above the compiler interfaces; retain tracked temporary aggregation while remaining definitions move.                                                      | Pack identities, inheritance and overrides unchanged; no compiler-verifier cycle.                        |
| 03  | Native content pass; integration pending | Divide `resources/*` into stocks, networks and processors, retaining generic definition/runtime splits. Move catalogue-dependent `stock_catalog.cpp` and `resource_bindings.cpp` to `content/resources`.                                                                                   | Selection, atomic transactions, fractional accounting and allocation contracts remain intact.            |
| 04  | Native content pass; integration pending | Move/split physical compilers into `content/equipment`, combat, operations, logistics and sensing. Move their exported definitions out of the umbrella interface with one declaration owner.                                                                                               | Real loader verification works without authoritative/runtime links.                                      |
| 05  | Native content pass; integration pending | Move `content/abilities.cpp`, effects/properties/rules/processes compilation and associated definitions into `content/effects`. Preserve shared immutable compilation scopes.                                                                                                              | No repeated compilation or schema changes introduced by interface splitting.                             |
| 06  | Native content pass; integration pending | Separate Console/Controller/profile/action compilers from knowledge/observation/tree binding compilers into `content/controllers` and knowledge. Allocate stations/bridge by actual function ownership.                                                                                    | Binding indices, command ownership and observation restrictions unchanged.                               |
| 07  | Native content pass; integration pending | Split UI layout/theme compilation into `content/ui`, asset compilation and Console/client asset packaging into `content/assets`, and scenario/conversation catalogue integration into `content/scenarios`.                                                                                 | Responsive layouts, verified assets and bounded client bundles preserved.                                |
| 08  | Native content pass; integration pending | Extract `WorkshopDocument`, draft resolution and `WorkshopHistory` to `content/documents`. Keep draft mutation separate from immutable Pack/Catalog identity.                                                                                                                              | Undo/import/dependency closure/export and incomplete-draft previews use production compilation.          |
| 09  | Active; gates pending                    | Move current network/session/replication/transport code into networking modules and platform transport/storage destinations. Keep declaration names initially. Split remote host/client and shared/host lobby targets where authority differs.                                             | Existing client/Workshop transitive exclusions; admission, recovery and transfer behaviour.              |
| 10  | Active; gates pending                    | Consolidate native/browser/Linux/Windows files, devices and lifecycle adapters into platform folders. Move semantic adapters to platform accessibility; introduce the narrow semantic contracts needed to detach them from Application.                                                    | Symbols, callback signatures, pointer routing and persistence still resolve on each platform.            |
| 11  | Active; gates pending                    | Extract GPU asset cache, renderer, radar and audio from `presentation`. Move sprite/model implementations behind graphics/scene; stop tools importing the broad presentation module to obtain rendering.                                                                                   | Runtime and Workshop share authorised renderer/assets with explicit teardown.                            |
| 12  | Active; gates pending                    | Move/split `core/console*` into `ui/console-model`. Separate action helpers and domain model formatting into ordinary implementation units. Move pure permitted projection readers out of core to their real consumers/shared knowledge projection contracts.                              | Model remains renderer/device/authority-free; client and local eligibility still agree.                  |
| 13  | Active; gates pending                    | Extract RmlUi runtime/resource registration from `screen.cpp` and `rml_backend.hpp`; extract shared focus/pad/semantic contracts and authoring Console preview into their module folders.                                                                                                  | Preview stays non-authoritative; multiple contexts, fonts, focus and stable semantics survive.           |
| 14  | Active; gates pending                    | Replace the large `consoles.hpp` class-body implementation with Console surface plus flight/tactical/engineering/operations/information widget implementations. Allocate `presentation` domain panel helpers to those folders.                                                             | Independently compiled widgets; one route owner for interaction/capture/cache state.                     |
| 15  | Active; gates pending                    | Separate menu/lobby/settings/loading presentation into UI screens and runtime scene/sidebar/cameras into UI Viewscreen. Reduce screen dispatch to composition of modules.                                                                                                                  | Native/browser share screens; region focus, controls, localisation and view permissions preserved.       |
| 16  | Active; gates pending                    | Split tool shell, document workflow, map/layout editors and material previews out of `tools/editor.cpp`, `layout_workspace.hpp`, `workshop.cpp` and helpers.                                                                                                                               | Editors use owned document/selection state and narrow graphics/UI preview interfaces.                    |
| 17  | Active; gates pending                    | Split `gameplay_editor.cpp` by domain into gameplay-editors and tree-editor; extract test-client and GM inspection. Keep native/browser disposable test hosts separately linked.                                                                                                           | GM no longer copies Application to masquerade as an authoring session; testing isolation retained.       |
| 18  | Active; gates pending                    | Split Application/menu responsibilities into shell, library, preferences, connections and displays. Move entry sources into executable folders; move platform main-loop glue out of role composition and explicit smoke orchestration into existing manual harnesses where appropriate.    | Startup forms, saves, display assignments, worker startup and browser role exports unchanged.            |
| 19  | Active; gates pending                    | Establish world/state, spatial, lifecycle, schedule and persistence interfaces. Move body/math/spatial/collision infrastructure to their owners. Expose minimal typed collaborators for future extractions; keep the original phase sequence executable during transition.                 | No public all-purpose World context; phase/teardown/save ownership is explicit.                          |
| 20  | Partial extraction                       | Extract fitted equipment/capability indexing from World into gameplay/fitting and authoritative `gameplay_resources*`, supply/resource preparation into resources/equipment. Split preparation, grant inspection and commit without changing accounting.                                   | One frozen resource allocation committed once; physical bindings retain stable identity.                 |
| 21  | Partial extraction                       | Extract effects/contracts, properties and transactions: `effect_transaction`, native outcome records/helpers, custom/property effects, effect application/store/damage preparation. Wire native operations through typed ports.                                                            | Atomic failure and stale-payment protection; move-only prepared transaction ownership retained.          |
| 22  | Partial extraction                       | Extract effects/lifecycle and rules: pending queue, payloads/receipts, rules state/serialization, sources, funding, region rules, maintained evaluation and persistence.                                                                                                                   | One invocation lifecycle, stable source IDs and original trigger/damage ordering.                        |
| 23  | Partial extraction                       | Extract effects/abilities and processes, including machinery, thermal and finite-work adapters. Keep native work drivers with their physical domain modules; retain one shared work/credit/completion execution path.                                                                      | Activation/recovery, pre-generation funding, native progress and exactly-once completion survive resume. |
| 24  | Partial extraction                       | Move generic knowledge to records and extract authoritative integration/projection snapshots into knowledge/consoles and projections. Extract control ownership, Controller execution/actions, coordination and provider policy from World fragments.                                      | Same human/AI knowledge, handover generations, inboxes and decision snapshots; no omniscient adapters.   |
| 25  | Partial extraction                       | Extract gameplay/flight, sensors and psychic from current flight/sensor/psychic fragments and definitions runtime. Put navigation assessment with flight, leaving Controller navigation intent in coordination.                                                                            | Same motion/sensing/reception physics, stable observations and permitted targets.                        |
| 26  | Partial extraction                       | Extract combat/directed/ordnance/destruction/shield runtime into gameplay/combat. Connect effect hooks/transactions through the already established ports; retain authored hit references after source disappearance.                                                                      | Same damage traversal, reaction scheduling, ordnance ordering and shield contributions.                  |
| 27  | Partial extraction                       | Extract personnel, teams, away, boarding and conversations. Move compartment routing to teams; other domains use its narrow route query. Allocate capture/ownership orchestration to boarding, with claim revocation through control ownership.                                            | Team/patient/fault progress, transport commitments, scope and capture authority remain correct.          |
| 28  | Partial extraction                       | Extract docking, craft, physical logistics/cargo and FTL. Keep team/away coordination through narrow domain interfaces and work execution through effects/processes.                                                                                                                       | Dock coupling, stock conservation, sortie continuity, servicing and deliberate departure/reports.        |
| 29  | Partial extraction                       | Extract scenarios/runtime, environment, objectives and integration from scenario/gameplay_scenario/mission/interdiction/field code. Generic conversations remain gameplay/conversations; interdiction physical operation remains gameplay/psychic while scenario orchestration lives here. | Supported scenario operations replace private World access; objective/debrief timing unchanged.          |
| 30  | Partial extraction                       | Finish World lifecycle/schedule/persistence delegation: move remaining compilation/spawn/restore/save/projection logic to its owners. Retain a thin World facade and explicit phase composition.                                                                                           | No gameplay implementation included inside World::Impl; ownership and restore ordering auditable.        |
| 31  | In progress                              | Remove obsolete umbrella imports/targets and old source folders. Finish family build composition and README maps; enforce all planned source/import/link rules and complete the migration evidence record.                                                                                 | Full native/Linux/ordinary-WASM/threaded-WASM gates; no unmapped sources or temporary fragments.         |

Rows are ordered to keep the repository buildable. If a row is too large, split it into named subchunks in the ledger before starting; keep its acceptance requirements and do not mark the parent complete until all subchunks pass. Existing working modules can move without redesign. Changes to public interfaces must include all consumers in the same verified logical step.

### Source ownership ledger and build organisation

Chunk 00 creates `docs/modularisation-ledger.csv` with columns `source_at_baseline,destination_module,chunk,status,evidence`. Inventory `.cpp`, `.cppm` and `.hpp` under `src/`, including diagnostics and adapter implementations. Resolve files with mixed responsibilities to multiple destination entries, listing the moved responsibilities in evidence; account for every original source once as an origin without pretending a split is a single-file rename. New production files get an empty baseline origin and a destination entry. Update the ledger in each migration commit.

Origin allocation follows the chunk table. In particular: core projection helpers are permitted read-model logic, not unrestricted authoritative access; native work/outcomes belong to the physical owner with contracts in effects; `network/blob*` belongs with platform/storage; platform atomic/random helpers belong to foundation/data while persistent store helpers belong to platform/storage. Unusual helpers must be allocated in chunk 00 rather than left in a final `misc` folder.

As targets move, give each module its local `CMakeLists.txt` and each family a `CMakeLists.txt` registering its child modules in dependency order. A parent may register a low-level adapter before a higher-level sibling family to satisfy the acyclic target graph. Keep pinned third-party setup at the root/existing dependency includes; keep test/executable registration organised separately. Move module file-set registration from the global ownership list into the owning module's build file and retain shared scanning/compiler settings centrally. Do not register the same module twice.

## Validation and chunk handoff

To resume this programme in a later task, read this plan, the ledger/evidence if present, the current module ownership reference and working-tree status. Select the earliest pending chunk; verify that earlier chunks' recorded changes are still present. Name the chunk in the task and commit messages. Finish its checks and receipt before selecting another chunk; a pause between chunks must leave a buildable repository. Do not re-run completed migrations or treat the inspection revision's test counts as current requirements.

### Before starting

- Record the revision and working-tree status. Preserve unrelated edits; do not reset other work. Rebase the ledger if files changed after the inspection baseline.
- Run the automatic native gate and native/WASM continuation checks on the starting revision. Record any existing failures separately. Derive current suite/trace lists from scripts rather than freezing historical counts in this plan.
- Capture representative pre-migration synthetic save artifacts and native continuation traces outside tracked production content. At the relevant migration chunk, restore those artifacts and compare continuation with an uninterrupted baseline run. Keep schemas and serialization compatible; do not use permanent production goldens.

### Every chunk

1. Update its source/destination ownership and allowed dependency edges, including imports, CMake file sets, scripts, test includes and browser linker/export references.
2. Format only the changed authored files. Reconfigure and build affected targets for native and WebAssembly so module scanning/export mistakes are caught immediately. Build relevant client/Workshop targets when shared interfaces or role exclusions change.
3. Run the affected existing synthetic system cases through production paths and affected authored-content verification. Compare continuation where execution, serialization or state ownership moved. Add a focused regression only for a concrete important uncovered contract.
4. Check forbidden dependencies and private includes. Inspect the resulting interface: callers must not need enclosing World/Application/editor state or another module's internals.
5. Record actual commands, results, limitations and commit IDs in `docs/modularisation-evidence.md`; update the ledger and this plan's chunk status. Commit each verified logical step. Do not push or deploy as part of this plan.

Useful gate commands, from [building](building.md):

```powershell
./scripts/check.ps1 -Target native
./scripts/check.ps1 -Target web -Threaded:$false
./scripts/check.ps1 -Target web -Threaded
```

Run web comparisons using native traces from the same revision, preserving all trace inputs described in the build guide. Run the complete native and both WASM gates at milestones after chunks 08, 18, 24, 30 and 31; otherwise use the affected checks above. Run the Linux gate from `scripts/check-linux.sh` at these milestones on a suitable Linux environment. Do not run real network timing cases while competing heavy builds are saturating the machine. Record unavailable platforms as remaining evidence, not passes.

### Contract scenarios to preserve

- Human and AI commands share eligibility/admission, handover generations and scoped observations; stale claims/actions fail safely.
- Resource allocations and effect transactions commit once or leave physical state unchanged; native work, fractional credits and completion survive cancellation/resume correctly.
- Scenario rules, damage/reactions, machinery funding and finite work retain fixed-tick ordering and uninterrupted-versus-restored equivalence.
- Local/remote/client/GM sessions, consensus recovery and durable storage retain role restrictions and compatible formats.
- Console region identities, neutral input release, focus/capture, accessible semantic actions and content invalidation retain their existing synthetic contracts.
- Workshop draft history, partial previews, valid export and disposable host execution continue through real production loaders without linking authority into the UI.
- Native/browser entry points, browser exports, ordinary/threaded worker composition and asset/graphics lifetimes remain valid.

Graphical/native multi-display, browser Test, real screen readers, devices, storage reload, public-network interoperability and performance remain explicitly triggered smoke evidence. Moving their implementation does not establish physical acceptance. Do not add automatic timing thresholds or a mandatory smoke per extraction.

### Final completion gate

All first-party C++ is under the family/module hierarchy; every baseline source is reconciled in the ledger; obsolete core/presentation/simulation/tools/entry/network/session/replication/transport layouts are removed once their contents migrate. Existing names such as `resources` and `behavior` remain as families with only module folders beneath them.

All completed modules compile independently of enclosing class fragments and obey declared dependency rules. World and application code compose owned modules; they do not expose universal mutable contexts. Save/wire/content compatibility, authoritative restrictions and worker lifetimes remain unchanged. All milestone checks have executed evidence or clearly identified outstanding environmental validation. Update `docs/modules.md`, README links and the implementation record to describe the delivered layout; retain this plan and receipts as history rather than claiming completion from the folder tree alone.
