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

`--input` accepts a source directory or ZIP/DAT pack. Repeat `--dependency <directory|zip|dat>` in dependency order. Archives use ordinary Workshop opening/resolution; directories load authored source without flattening inherited fields or requiring a finished playable pack. Dependencies remain explicit.

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

Use `set`/`unset` for ordinary fields, `merge` for RFC 7396 patches and `patch` for RFC 6902 structural edits. These also cover custom gameplay data, ship bindings and structured editor content. Use semantic operations for reference-repairing renames, map/instance actions, state reparent/rename, ship-item creation, flattening and layout drops. `vocabulary` with a tree target exposes its current action/observation signatures, including Controller bindings and owner-local policies. Coordinates use the same authored units as the GUI: map positions in millimetres, headings in millidegrees, layout coordinates in their authored layout space.

`describe` returns the shared standard field catalogue. Query one field with `{"field":"gameplay-editors.equipment.charge_per_second"}`. Passing its ID as `descriptor` to `set` applies the same type/static-choice checks as the GUI. Query record prototypes with `{"records":true}` or `{"record":"gameplay-editors.combat.decks"}`; `append` accepts a `prototype` ID in place of `value` and generates fresh record identities using the same operation as the GUI. Context-specific choices remain available through `choices` and inspection of the current document; production verification checks the completed bindings.

`batch` accepts document operations, including binary imports, and commits them as one undo step. Any failure discards the candidate. Add `verify: true` to editing arguments or batch arguments to require full production verification before publication, including custom connection edits. Filesystem `open`, `save`, `export` and history commands are separate session actions. Undo/redo includes imported assets and dependencies. Structural errors such as duplicate definition IDs are rejected at publication; unfinished gameplay fields are permitted until explicit verification/packaging. Binary assets remain bounded by production pack limits.

## GUI integration and extension

The GUI keeps a mutable presentation buffer for text input and cancelled gestures. `WorkshopAuthoring` publishes completed edits and owns history; its document accessor is const. Pack/map saves publish the buffer before exporting. Native **File > Source draft directory…** opens/saves the same directory format. Both platforms can open/save a source draft ZIP without requiring gameplay verification; that action does not replace the normal pack save destination.

The `phoenix.workshop.authoring_core` module owns the operation registry, shared field catalogue, record prototypes and structural layout/tree operations. Standard GUI inspectors read those descriptions; CLI discovery reads the same definitions. Custom canvases keep their visual/input code and use the shared document transactions and structural operations. Generic structured edits remain available through the registered patch/merge operations.

Add fixed fields/choices to `fields.json` and use `edit_registered` or `edit_shared_field` in the GUI. Add record defaults to `workshop_records`; GUI `records` and CLI `append` share them. Add semantic operations with their descriptions, argument types and handlers together in the operation registry. Do not add a separate CLI content schema or authoritative mutable GUI document.

The automatic architecture check catches unknown field references, new fixed GUI-only scalar/static-choice declarations, GUI-owned history and rendering/host imports into the core. CMake additionally prohibits transitive graphics and simulation dependencies. Synthetic authoring integration checks exercise transactions, identities, shared GUI/CLI publication, draft round-trips, dependency overrides and package equivalence. Graphical, physical-device and accessibility acceptance remain separate smoke evidence.

## Verification receipt � 31 October 2026

The Windows native CLI and Workshop libraries build with the pinned Visual Studio toolchain. The complete native application links, and the threaded browser Workshop builds with Emscripten. The affected native gate passes `system.workshop_test`, `system.content-and-packs`, `system.workshop-authoring`, `system.workshop-cli`, `architecture.module-boundaries` and `architecture.workshop-authoring`.

Synthetic checks cover GUI/CLI publication equivalence, batch rollback, refused reads without document mutation, revision conflicts, shared record identities, asset undo/redo, source/ZIP/DAT round-trips, editable PNG restoration from DAT, verified-edit rollback, inherited overrides, dependency cleanup on source opening, state reference repair and layout wrapping/movement. Process tests exercise one-shot/session equivalence, malformed-line recovery, UTF-8 content and Windows paths, stdin arguments, exit codes and preservation of existing files after refused edits.

The production verifier accepts the current base plus Test Missions closure: 55 entity definitions, 8 maps and 63 Consoles. These are inventory counts from this run, not assertions on shipped content. No gameplay save schema changes.

Native verification used one explicit CMake configuration and then ordinary MSBuild compilation with only repeated regeneration rules suppressed through an ignored local targets file. Parallel regeneration in the shared build tree had collided on generated dependency files. The application, compiler and tests were not bypassed.

Linux-native execution, new GUI source-dialog interaction, browser graphical journeys and physical accessibility/device acceptance were not run for this change. The source writer stages and rolls back ordinary failures; it does not promise a multi-file transaction across sudden power/process loss.
