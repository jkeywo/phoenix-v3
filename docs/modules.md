# Modules and reading guide

Code is grouped as `src/<family>/<module>/`. Start with a family's README, then the selected module's named interface and its implementation. Folders group related reading; named modules define declaration ownership; CMake targets define compiled/link ownership. These are related boundaries, not interchangeable counts.

The [interactive module graph](module-graph.html) is a self-contained HTML snapshot with family grouping, code sizes, search, zoom/pan and dependency highlighting. Arrows represent actual C++ imports between folders, including private partitions; external libraries, same-folder imports, CMake-only links and runtime calls are excluded. Regenerate it from the current source with `python scripts/module-graph.py`. Its template is `tools/module-graph/template.html`; counts and ownership use the same source scanner as the line inventory. It requires no web service or downloaded JavaScript.

The [migration plan](modularisation-plan.md), [source ledger](modularisation-ledger.csv) and [verification receipts](modularisation-evidence.md) record intended work, actual ownership and checks performed. The ordinary World outline and domain interfaces are active. Native bindings intentionally retain cross-domain integration policy; that policy is visible in the reading map rather than presented as independent domain libraries. Source ownership and execution acceptance are recorded separately.

The migration cohort passed the full native build (66), all 48 native tests (68), 23 fresh bit-exact continuation comparisons against `34883ac` and 12 live-network cases (69), and ordinary-WASM full build/runtime/continuation gates (65/70). Threaded-WASM full build/runtime/continuation gates also passed (72/74). These receipts do not establish Linux-native compilation, physical multi-display/device/accessibility checks or public-network smoke acceptance. See the evidence record for scope and timing context.

The subsequent [World/Console refinement](understandability-refactor.md) separates native workflow chapters, complete conversation/navigation panel state, shared document services and data-defined choice presentation. Its checks and growth are recorded separately from the migration cohort above.

## Reading map

| Family      | Start here                                                            | Owns                                                                           |
| ----------- | --------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| Foundation  | [data, contracts, input and eligibility](../src/foundation/README.md) | Portable values, bounded IO, shared commands/observations and logical input    |
| Behavior    | [programs and vocabulary](../src/behavior/README.md)                  | Typed state-tree compilation/execution and stable gameplay vocabulary          |
| Content     | [compiler layers](../src/content/README.md)                           | Packs, immutable definitions, verification, assets and editable documents      |
| Resources   | [stocks and native accounting](../src/resources/README.md)            | Finite stores, processor claims, power/cooling networks and fitted allocation  |
| Knowledge   | [record/Console owners](../src/knowledge/README.md)                   | Scoped records, provenance, messages and permitted decision projections        |
| Control     | [Controller integration](../src/control/README.md)                    | Claims, bindings, proposals, tree actions, coordination and admission          |
| Effects     | [shared effect mechanisms](../src/effects/README.md)                  | Rules, properties, invocation, work/funding, grants and transactions           |
| Gameplay    | [native domain mechanisms](../src/gameplay/README.md)                 | Flight, combat, personnel, logistics, sensors and other native services        |
| World       | [authoritative composition](../src/world/README.md)                   | Actor lifetime, registry ownership, fixed phase order and candidate restore    |
| Scenarios   | [regions, execution and integration](../src/scenarios/README.md)      | Exact environment geometry, situations, objectives and native adapters         |
| Networking  | [protocol and role separation](../src/networking/README.md)           | Wire transfer, consensus, durability, client/host sessions and lobby           |
| Graphics    | [render resources and scene](../src/graphics/README.md)               | GPU/audio resources and permitted scene/radar rendering                        |
| UI          | [models, surfaces and widgets](../src/ui/README.md)                   | Renderer-free Console state, per-route interaction and presentation            |
| Workshop    | [authoring and GM surfaces](../src/workshop/README.md)                | Documents, editors, previews and disposable test requests                      |
| Application | [composition services](../src/application/README.md)                  | Screen/session state, installed content, preferences, displays and connections |
| Platform    | [OS/browser adapters](../src/platform/README.md)                      | Files, devices, storage, transport, accessibility and lifecycle                |
| Executables | [runnable roles](../src/executables/README.md)                        | Native/browser startup, content tools, worker hosts and explicit diagnostics   |

## Public owners and dependencies

First-party APIs are C++20 named modules. Import the owner you use and link its actual target. Implementations often need private imports in addition to the types exposed by their public interface; a visible Catalog or Observation does not implicitly export every compiler or runtime operation.

| API                                                                              | Target or target choice                                                                                | Reading seam                                                                                                  |
| -------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------- |
| `phoenix.data`, `phoenix.contracts`, `phoenix.input`, `phoenix.console_controls` | `phoenix_data`, `phoenix_contracts`, `phoenix_input`, `phoenix_console_controls`                       | Foundation leaves; the remaining foundation link convenience groups these targets without owning declarations |
| `phoenix.state_tree`                                                             | `phoenix_behavior_state_tree_definitions` for compilation; `phoenix_behavior_state_tree` for execution | Same named contract, distinct compile/runtime ownership                                                       |
| `phoenix.resources.stocks`, `phoenix.resources.processors`, `phoenix.resources`  | Corresponding stocks/processors/networks definition targets, with runtime targets only when executing  | Generic accounting remains below content/equipment adapters                                                   |
| `phoenix.knowledge`, `phoenix.conversations`, `phoenix.scenario`                 | Record/conversation/scenario definition targets or their runtime counterparts                          | Definition inspection need not link authoritative execution                                                   |
| `phoenix.scenarios.environment`                                                  | `phoenix_scenarios_environment`                                                                        | Pure regions, exact geometry, attenuation and deterministic field generation                                  |
| `phoenix.content.<domain>.types`                                                 | `phoenix_content_<domain>_types`                                                                       | Unique compiled definition owners                                                                             |
| `phoenix.content.<domain>` compiler APIs                                         | `phoenix_content_compilers` for the mutually recursive compiler implementation group                   | Separate acyclic public APIs; a deliberate compiler SCC is not independent domain libraries                   |
| Packs/catalogue/assets/UI/documents/distribution                                 | Their `phoenix_content_<module>` targets                                                               | Separate archive, model, asset, layout, editing and permitted packaging ownership                             |
| `phoenix.application`, `phoenix.navigation`, `phoenix.console`                   | `phoenix_application_shell`, `phoenix_ui_navigation`, `phoenix_console_model`                          | Application state, semantic navigation and per-route Console model are separate                               |
| `phoenix.session.api`                                                            | `phoenix_session_api`                                                                                  | Shared permitted session contract; local/host implementations remain separate                                 |
| `phoenix.simulation`                                                             | `phoenix_simulation`                                                                                   | Public World facade and authoritative composition                                                             |

The content re-export facade, generic gameplay definition/runtime aliases and Core compatibility target have been retired. New dependencies should name an actual owner. Console's renderer-free API includes eligibility, message and public-readout free functions as well as ConsoleModel; inspect the whole interface when deciding whether its dependency is needed.

Content compiler implementations retain real recursive relationships among combat/directed definitions, effects/rules/abilities/processes and stock/logistics resolution. Their shared implementation target is intentional and documented. Packs, image codec, catalogue model, assets, UI, documents and distribution remain separately owned. Public declaration ownership must not be duplicated to disguise a cycle.

World compiles ordinary implementation units in twelve private workflow folders under `src/world/`, with core actor assembly in `native-actors`, explicit phase order in `schedule`, and candidate checkpoint composition in `persistence`. `:runtime`, the workflow partitions, `:schedule` and `:persistence` are private to the simulation module. See the [World reading map](../src/world/README.md) rather than the retired mixed binding bucket. The partition folders do not claim independent authoritative targets; remaining integration policy is explicit.

Console surfaces compose [document services/choice descriptions](../src/ui/surface-host/README.md), [conversation state](../src/ui/information-widgets/README.md) and [navigation/mission panels](../src/ui/navigation-panel/README.md). Those panel interfaces consume permitted models and bounded frame/publication records without importing Application. Layout and other specialized composition remain in the surface host.

## Authoring a module

Keep the interface beside its implementations and register it with a public `CXX_MODULES` file set in the module's local CMake. Put standard/third-party includes in the global module fragment; import the Phoenix APIs whose declarations you use. Importing a module does not promise incidental standard-library includes.

```cpp
module;
#include <string>

export module phoenix.example;
export import phoenix.contracts;

export namespace phoenix
{
std::string describe(const BodyView &body);
}
```

The implementation attaches to `module phoenix.example;`. Give each exported class, record and free function one owner. Canonical serializers belong with their records; do not copy their declarations/bodies into another named module. Keep non-template JSON operations out of exported method bodies when an out-of-line implementation is appropriate. Shared template/constant definitions use explicit inline semantics where required by named modules.

Track both imports and target links. The source checker checks named ownership/private-header boundaries; CMake checks linked dependency reachability. Neither replaces compilation, runtime integration or permission/continuation checks. Use the build commands and compiler constraints in [building.md](building.md), with test policy in [testing.md](testing.md).

## Source size inventory

The current snapshot has **682 first-party C++ files in 148 module folders**, with **216 primary named modules and 15 private partitions**. Consult the generated [inventory](module-sizes.md) for current line counts and source fingerprint. These are reading sizes, not runtime or platform acceptance claims.

Physical lines include comments, blank lines, directives and embedded
JavaScript/JSON/text. Nonblank lines exclude whitespace-only lines but still
include comments. Only maintained C++ source under `src/` is counted; tests,
vendors, historical references, generated build outputs, CMake and external web
assets are excluded. The maintained includes-only native headers count.

The [full family/module table](module-sizes.md), [module CSV](module-sizes.csv)
and [per-file CSV](module-files.csv) give both counts and the named-unit split.
The inventory records a normalized source fingerprint; regenerate it after an
actual gate-driven source fix. CMake-only composition edits do not affect these
counts.

| Family        | Module folders | Physical lines | Nonblank lines |
| ------------- | -------------: | -------------: | -------------: |
| `application` |              6 |          1,440 |          1,406 |
| `behavior`    |              2 |          2,108 |          2,094 |
| `content`     |             17 |         21,175 |         20,969 |
| `control`     |              5 |          2,922 |          2,836 |
| `effects`     |              8 |          7,026 |          6,752 |
| `executables` |              7 |          2,531 |          2,491 |
| `foundation`  |              4 |          1,882 |          1,858 |
| `gameplay`    |             23 |         12,459 |         12,278 |
| `graphics`    |              5 |          4,425 |          4,373 |
| `knowledge`   |              4 |          4,588 |          4,489 |
| `networking`  |             10 |          9,458 |          9,342 |
| `platform`    |              7 |          4,990 |          4,897 |
| `resources`   |              4 |          3,967 |          3,918 |
| `scenarios`   |              5 |          5,101 |          5,057 |
| `ui`          |             12 |         20,292 |         19,975 |
| `workshop`    |              9 |         10,135 |          9,915 |
| `world`       |              6 |         31,039 |         29,948 |

The implementation refines the plan's original folder sketch with these concrete seams. They remain inside the intended family and have their own reading guides.

| Additional folder              | Reason for the boundary                                                                                                            |
| ------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| `application/preference-store` | Application-aware settings persistence stays above portable preference/localisation values                                         |
| `content/distribution`         | Permitted launcher/client packaging sits above verification and asset compilation, avoiding a reverse asset-to-verifier dependency |
| `effects/funding`              | Input identities, fractional credits and fee plans are shared by rule/ability/process funding without owning native stores         |
| `gameplay/combat-continuation` | Native restore/schema validation uses explicit immutable identities rather than World lookup                                       |
| `gameplay/damage`              | Shield interception, ordered absorption and structural damage plans are reusable native mechanisms                                 |
| `gameplay/directed`            | Directed mode selection, qualification and operation receipts have one owner                                                       |
| `gameplay/machinery`           | Native process eligibility consumes bounded actor facts instead of a World context                                                 |
| `gameplay/ordnance`            | Missile/contact geometry, blast exposure and material traversal stay below native phase composition                                |
| `gameplay/ordnance-contacts`   | Tick-local contact indexes and deterministic fuse/contact ordering are separate from participant authority                         |
| `gameplay/repairs`             | Native structural/fault repair progress is separate from team scheduling and access                                                |
| `gameplay/weapons`             | Magazine/ammunition commitment and projectile initialization use bounded store/definition views                                    |
| `graphics/geometry`            | Local navigation/ship camera and sprite-facing calculations need no GPU or Application                                             |
| `knowledge/read-models`        | Clients decode already-permitted information without acquiring authoritative knowledge execution                                   |
| `networking/projection`        | Bounded observation/public-board envelopes and permission validation stay outside mutable sessions                                 |
| `platform/viewport`            | Focus and CSS/drawing-surface synchronization stay outside UI/Application state                                                    |
| `scenarios/interdictions`      | Encounter orders, rosters and continuation are separate from physical psychic simulation                                           |
| `gameplay/projectiles`         | Canonical projectile launch/continuation and native owner records are separate from geometry and contact indexing                  |
| `world/` workflow integrations | Ordinary private World implementation units expose cross-domain integration policy without class-body includes                     |

Record a new owner's responsibility and dependency direction rather than renaming it merely to resemble the initial sketch.

## Validation status

Per-revision build, system, trace and role receipts are maintained in [modularisation-evidence.md](modularisation-evidence.md). Native, ordinary WebAssembly and threaded WebAssembly are separate gates. Browser rendering, real screen readers, device routing, OS-native adapters and performance captures require their own explicitly invoked evidence. No current full-browser/runtime acceptance is inferred from this guide or a compiled export table.

The section below preserves an earlier compiler-conversion receipt. Its timings and case counts describe 17 October, not the present family migration or its performance.

## Historical conversion evidence — 17 October 2026

The pre-conversion baseline and converted automatic gate passed: ten native integration cases, the WASM checks (including the newly added input/semantics case), native/WASM continuation comparison, and five network-service cases covering discovery/relay, plaintext-bind refusal, live fleet recovery, client/GM sessions and trusted/untrusted WSS.

Clean builds rebuilt all targets and dependencies successfully. The following local Release timings used eight build workers, MSVC 19.38 with the Visual Studio generator, and Emscripten 6.0.12/Clang 24 with Ninja. These are single observations, not a before/after performance comparison.

| Build experiment                                               | Native   | WASM     |
| -------------------------------------------------------------- | -------- | -------- |
| All targets, `--clean-first`                                   | 104.64 s | 109.90 s |
| All targets, no source changes                                 | 2.15 s   | 0.18 s   |
| Touch input implementation; build input/semantics target       | 1.90 s   | 3.56 s   |
| Touch contracts interface; build simulation integration target | 9.33 s   | 10.87 s  |

The implementation experiment rebuilt input and relinked its test without recompiling unrelated consumers. The interface experiment rebuilt contracts, the dependent simulation interface/implementation and its test. These were timestamp invalidation probes; no test-only API or behavior changes were retained. Native's all-target build also invokes the existing training-pack generation target, so its no-change result is not a pure compiler measurement.

Compiled server/client/Workshop export tables retain their semantic, file, transport and applicable storage callbacks. The minified storage/blob probes retain their JavaScript `Module` callback bindings for `ccall`. This checks linkage and exported names, not actual browser storage or accessibility operation.
