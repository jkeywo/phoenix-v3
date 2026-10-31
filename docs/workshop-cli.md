# Workshop command-line authoring

`phoenix_workshop` exposes Workshop content authoring to scripts and LLMs without opening a graphics window. It edits the same document types and uses the same verification, inheritance, imports and packaging as the GUI. Preview rendering and gameplay tests stay in the GUI.

Build with `./scripts/build.ps1 -Target native -BuildTarget phoenix_workshop`. On Windows the executable is `build/native/Release/phoenix_workshop.exe`; Linux builds place it at `build/linux/phoenix_workshop`.

## Discovery and ordinary commands

```text
phoenix_workshop capabilities
phoenix_workshop describe instance.edit
phoenix_workshop describe --args-file field-query.json
phoenix_workshop list --input ./my-mod
phoenix_workshop get --input ./my-mod --args-file target-query.json
phoenix_workshop batch --input ./my-mod --requests edits.json --output ./my-mod
phoenix_workshop verify --input ./my-mod
phoenix_workshop export --input ./my-mod --output ./my-mod.zip
phoenix_workshop export --input ./my-mod --output ./my-mod.dat
```

`--input` accepts a source directory or ZIP/DAT pack. Use `--source <directory|zip>` to open unfinished authored source explicitly, without gameplay verification. Repeat `--dependency <directory|zip|dat>` in dependency order. Archives use ordinary Workshop opening/resolution; directories load authored source without flattening inherited fields or requiring a finished playable pack. Dependencies remain explicit.

Every editing invocation requires `--output`. A directory output saves a source draft, including unfinished content. ZIP/DAT output verifies and exports a runnable pack and its launcher/client companions. Verification failures preserve existing output. Writes stage all files before replacing destinations and roll back replaced files if a later replacement fails; sudden process/power loss is not a multi-file atomic transaction. Source saves preserve unrelated files and refuse traversal/symlink destinations. Deleted source files are not automatically removed from an existing output directory.

Use `--args-file <file|->` for an operation's arguments, or `--args <JSON>`. `--requests <file|->` supplies a JSON array for `batch`. File/stdin arguments avoid shell quoting problems. Default stdout is JSON; stderr contains process diagnostics. Exit codes are `0` success, `1` operation/verification refusal and `2` invocation/protocol failure. A persistent session reports individual request failures without terminating.

## Requests, targets and drafts

`phoenix_workshop session --input ./my-mod` reads one request per stdin line and flushes one response per stdout line. It retains history and loaded dependencies. EOF exits without saving. No GUI process or network listener is involved.

```json
{"version":1,"id":"read-ship","operation":"get","arguments":{"target":{"kind":"entities","id":"my.ship"}}}
{"version":1,"id":"name-ship","operation":"set","arguments":{"target":{"kind":"entities","id":"my.ship"},"field":"name","value":"Mercury"}}
{"version":1,"id":"save-draft","operation":"save","arguments":{"path":"./my-mod"}}
```

Responses contain `version`, the caller's `id`, `ok`, `revision`, and either `result` or an `error` with code, operation, target where available and message. Supply `expected_revision` alongside `operation` to reject stale edits. The protocol version is independent of gameplay save schemas.

Targets use `kind` and definition `id`, with an optional relative JSON Pointer `path`. Alternatively use `scope: "manifest"`, `scope: "definitions"` or `scope: "map"`. `get` with `resolved: true` resolves inheritance without changing authored source. For entities, `fitted: true` additionally exposes expanded fitting and assembly-origin metadata used by the ship graph. `list` identifies local/dependency definitions; `choices` returns current production-resolved IDs. `override` explicitly creates an editable local dependency override.

For collections that may change order, call `target` with the existing path. It returns identity-bearing `segments`; use that returned target in subsequent requests. Records with IDs follow those IDs through insertion/deletion. Unnamed array values retain positional addressing. Missing identities fail rather than selecting a different record.

Use `set`/`unset` for ordinary fields, `merge` for RFC 7396 patches and `patch` for RFC 6902 structural edits. These also cover custom gameplay data, ship bindings and structured editor content. Entity, Console, UI and tree renames use their namespace-specific reference policies. Other database renames repair local inheritance; use `verify` to diagnose remaining external references. Renames preserve prose and equal IDs in other namespaces. Use semantic operations for map/instance actions, state reparent/rename, ship-item creation, flattening and layout drops. `vocabulary` with a tree target exposes its current action/observation signatures, including Controller bindings and owner-local policies. Coordinates use the same authored units as the GUI: map positions in millimetres, headings in millidegrees, layout coordinates in their authored layout space.

`describe` returns the shared standard field catalogue. Query one field with `{"field":"gameplay-editors.equipment.charge_per_second"}`. Passing its ID as `descriptor` to `set` applies the same constraints and coupled changes as the GUI. Use `control.set` for contextual controls. Query record prototypes with `{"records":true}` or `{"record":"gameplay-editors.combat.decks"}`; `append` accepts a `prototype` ID in place of `value` and generates fresh record identities using the same operation as the GUI. Context-specific choices remain available through `choices` and inspection of the current document; production verification checks the completed bindings.

`batch` accepts document operations, including binary imports, and commits them as one undo step. Any failure discards the candidate. Add `verify: true` to editing arguments or batch arguments to require full production verification before publication, including custom connection edits. Filesystem `open`, `save`, `source.export`, `map.export`, `export` and history commands are separate session actions. Undo/redo includes imported assets and dependencies. Structural errors such as duplicate definition IDs are rejected at publication; unfinished gameplay fields are permitted until explicit verification/packaging. Binary assets remain bounded by production pack limits.

## GUI integration and extension

The GUI keeps a mutable presentation buffer for text input and cancelled gestures. `WorkshopAuthoring` publishes completed edits and owns history; its document accessor is const. Pack/map saves publish the buffer before exporting. Native **File > Source draft directory…** opens/saves the same directory format. Both platforms can open/save a source draft ZIP without requiring gameplay verification; that action does not replace the normal pack save destination.

The `phoenix.workshop.authoring_core` module owns the operation registry, shared field catalogue, record prototypes and structural layout/tree operations. Standard GUI inspectors read those descriptions; CLI discovery reads the same definitions. Custom canvases keep their visual/input code and use the shared document transactions and structural operations. Generic structured edits remain available through the registered patch/merge operations.

Add fixed fields/choices to `fields.json` and use `edit_registered` or `edit_shared_field` in the GUI. Add record defaults to `workshop_records`; GUI `records` and CLI `append` share them. Add optional-block defaults to `workshop_seeds` and use `workshop_seed` in the GUI; CLI `describe` with `{"seeds":true}` and `seed` expose those same values. Add semantic operations with their descriptions, explicit argument types, execution class and handlers together in the operation registry. The CLI derives query/output handling from that execution class. Do not add a separate CLI content schema or authoritative mutable GUI document.

The automatic architecture check catches unknown field references, new fixed GUI-only scalar/static-choice declarations, GUI-owned history and rendering/host imports into the core. CMake additionally prohibits transitive graphics and simulation dependencies. Synthetic authoring integration checks exercise transactions, identities, shared GUI/CLI publication, draft round-trips, dependency overrides and package equivalence. Graphical, physical-device and accessibility acceptance remain separate smoke evidence.

## Verification receipt — 31 October 2026

The Windows native CLI and Workshop libraries build with the pinned Visual Studio toolchain. The complete native application links, and the threaded browser Workshop builds with Emscripten. The affected native gate passes `system.workshop_test`, `system.content-and-packs`, `system.workshop-authoring`, `system.workshop-cli`, `architecture.module-boundaries` and `architecture.workshop-authoring`.

Synthetic checks cover GUI/CLI publication equivalence, batch rollback, refused reads without document mutation, revision conflicts, shared record identities, asset undo/redo, source/ZIP/DAT round-trips, editable PNG restoration from DAT, verified-edit rollback, inherited overrides, dependency cleanup on source opening, state reference repair and layout wrapping/movement. Process tests exercise one-shot/session equivalence, malformed-line recovery, UTF-8 content and Windows paths, stdin arguments, exit codes and preservation of existing files after refused edits.

The production verifier accepts the current base plus Test Missions closure: 55 entity definitions, 8 maps and 63 Consoles. These are inventory counts from this run, not assertions on shipped content. No gameplay save schema changes.

Native verification used one explicit CMake configuration and then ordinary MSBuild compilation with only repeated regeneration rules suppressed through an ignored local targets file. Parallel regeneration in the shared build tree had collided on generated dependency files. The application, compiler and tests were not bypassed.

Linux-native execution, new GUI source-dialog interaction, browser graphical journeys and physical accessibility/device acceptance were not run for this change. The source writer stages and rolls back ordinary failures; it does not promise a multi-file transaction across sudden power/process loss.

## Content-authoring parity audit — 31 October 2026

The follow-up audit found gaps in the original implementation: generic JSON reachability did not establish semantic parity. Tree defaults and local/reusable conversion, layout palette transformations, ship graph binding rules, standalone map export and unfinished source ZIP export needed shared operations. Those paths now use the authoring core from both interfaces.

| GUI content area                                        | CLI authoring surface                                                                                        | Shared behavior / drift control                                                                                         |
| ------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------- |
| Pack identity, parent and dependencies                  | `pack.rename`, `pack.parent`, `import`, `open`, `new`                                                        | Production document operations; parent ancestry validation                                                              |
| Definitions and inherited overrides                     | `list`, `get`, `create`, `defaults`, `override`, `flatten`, `rename`, `delete`                               | Dependency reads, explicit overrides, definition factories                                                              |
| Map and entity placement                                | `map.create`, `map.select`, `instance.*`, `map.export`                                                       | Production map actions, bulk fields, overrides and serialization                                                        |
| Gameplay databases and nested records                   | `describe`, `set`, `append`, `seed`, `patch`, `merge`, `unset`                                               | Shared scalar/choice descriptions, record prototypes and optional-block defaults                                        |
| Ship setup and connection canvas                        | `ship.add`, `ship.inspect`, `ship.override`, `ship.connect`, `ship.disconnect`, `ship.remove`                | Fitted node discovery, compatible ports, assembly addresses and verified graph edits                                    |
| Reusable and owner-local state trees                    | `vocabulary`, `tree.localize`, `tree.promote`, `tree.state.*`, `tree.task.*`, `tree.rename`, `tree.reparent` | Production vocabulary, source encodings, state reference checks, action signatures                                      |
| Console widgets, layouts and themes                     | `widget.add`, `layout.*`, `theme.*`, structured edits                                                        | Shared conversion, detach/override/revert, palette insertion, duplication, grouping, compact layouts and theme creation |
| Artwork, material/grid/dust, UI tokens and localisation | Imports and structured edits at their authored targets                                                       | Same authored document and production asset/UI parsers; dynamic defaults/choices still depend on the selected content   |
| Editable drafts and runnable packages                   | `save`, `source.export`, `export`, `verify`, `undo`, `redo`, `batch`                                         | Shared source serialization, production verification, packaging and transactions                                        |

`describe` exposes operations with explicit types, execution class and platform availability. It also exposes field descriptions, record prototypes and optional-block seeds. Example: `phoenix_workshop describe --args '{"seeds":true}'`. `seed` takes `target` and `seed`; its entry specifies which optional field to initialize. `defaults` takes a definition `kind` and optional tree `context`. `source.export --output draft.zip` preserves unfinished source; `map.export --output map.json` follows the GUI's verified map save path.

The architecture check now tokenizes balanced C++ arguments across Workshop source and headers. Regression fixtures include multiline declarations, comments and field IDs with spaces. It rejects new fixed scalar/choice declarations, fixed numeric helper ranges, optional-block constructors and fixed record constructors in authoring panels unless they use the shared catalogues. It checks semantic adapter presence and core dependency boundaries. The shared custom-control migration below expands this gate to reject raw authored-data widgets and GUI-owned defaults; custom behavior belongs in the backend registry.

The synthetic journeys compare CLI results with the functions called by GUI adapters for layout insertion and inherited ship overrides. They also exercise tree defaults/inheritance, owner-local copy/promotion, action signatures, refused referenced-state deletion, optional-choice erasure, dependency reads, layout widget duplication, template detachment with overrides, compact layouts, binary bounds and both export formats. These comparisons test content behavior; they do not drive every graphical control. Generic structured edits provide the remaining authored-field surface and deliberately permit unfinished data. Rendering, runtime playtests, selection/navigation, window arrangement, file dialogs and physical accessibility/device interaction are outside CLI content authoring.

Audit validation: all six affected native content/Workshop/architecture suites pass after rebuilding the CLI and affected test executables. The complete native application links, and the threaded browser Workshop builds. The graph journey checks a verified connection, refusal to remove a required physical binding and source-preserving assembly overrides. Batch checks include per-child verification before subsequent repairs. This audit did not run new graphical journeys or Linux-native execution.

## Shared custom controls — 31 October 2026

The remaining content-specific data-entry controls now use the shared backend. This includes sprite frames and attachments, billboard/collision conversions, stars/models, lighting, grid/dust platform overrides, region presentation, theme tokens/artwork/slices, Controller argument defaults, typed properties, tree binding sources, effect conversions and dynamic record defaults. Instrument dragging uses the shared snapping/bounds action. Bulk-instance fields and layout palette options are backend queries.

- `controls` returns the catalogue. With `target` and `control`, it returns the current value, effective default, authored path, bounds and choices. The descriptor's `context` schema declares required contextual identifiers such as an attachment, artwork slot, texture index or platform.
- `control.set` takes `target`, `control`, `value` and optional `context`. The backend validates the value and applies coupled changes. The GUI uses this same setter.
- `authoring.actions` exposes custom transformations and their argument schemas. `authoring.apply` takes `target`, `action` and an `arguments` object. Registration exposes both discovery and execution automatically.
- `record.defaults` resolves a `prototype` with optional `context`. `append` accepts the same context. For example, `effect.cost` accepts `quantity: "amount"` or `"per_second"`; `scenario.report` accepts the current `slots` array.
- `instance.fields` describes common fields for `ids`, including mixed values and shared sprite choices. `instance.field` applies `path` and `value` through that schema and production verification.
- `layout.palette` takes a Console `target` and optional `header`/`context`, returning component targets and ship/Console contexts. `asset.choices` applies the GUI image-picker filters, with optional `skybox: true`.

For example, this session request edits a marker using bounds derived from its authored atlas frame:

```json
{"operation":"control.set","arguments":{"target":{"kind":"entities","id":"my.ship","path":"/render/sprite/frames/0"},"control":"frame.marker.x","context":{"attachment":"engine_1"},"value":40}}
```

The automatic gate rejects new raw data-entry widgets in authoring panels, GUI-owned fixed/dynamic-key defaults, inline record constructors and unregistered descriptor/group/action references. Audited raw bindings are confined to transient view state and pending arguments in `src/workshop/shell/view-bindings.json`; previews, selection and pending input are not authored content. New content controls belong in the shared catalogue or action registry, which automatically supplies the CLI surface. Generic structured editing remains available for unfinished drafts.

Synthetic journeys exercise GUI-helper/CLI equivalence, coupled changes, dynamic vector bounds and paths, reference-safe publication, rejected edits, inherited platform defaults and texture layers, factories, gestures, typed bindings and backend-driven bulk fields. These are content-behavior checks, not a claim that every graphical interaction or physical accessibility journey was run.

Migration validation: the six affected native content/Workshop/architecture suites pass with the new control, factory, bulk-inspection and CLI-process journeys. The complete native application links, and the threaded browser Workshop builds. Changed C++/JSON/CMake/Markdown formatting and whitespace checks pass. No new graphical journey, Linux-native run or physical accessibility/device acceptance was performed.
