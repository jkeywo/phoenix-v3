# Headless Workshop authoring

Read `core.cppm` for the shared session, read-only published document and candidate-operation interfaces. `operations.cpp` registers document operations and descriptions together; `core.cpp` owns request dispatch, transactions and history. Const query handlers read the published document directly, or the current candidate inside a batch. `filesystem.cpp` is native-only persistence.

`fields.json` owns fixed and contextual control descriptions: defaults, choices, vectors, bounds, optional values, groups and coupled change handlers. CMake embeds it so discovery needs neither resources nor a window. `controls.cpp` resolves contextual descriptions and applies the shared field rules. The GUI renderer in `shell/fields.cpp` and CLI `controls`/`control.set` use that same path; ordinary `set` with a descriptor also uses it.

`custom_actions.cpp` registers each custom transformation with its argument schema and callback. GUI controls call those callbacks through `workshop_custom_action`; CLI discovery and `authoring.apply` are generated from the registry. `inspection.cpp` supplies the bulk-instance field tree and layout palette to both interfaces. Asset compatibility, colour conversion, sprite calibration, Controller arguments and tree binding encodings belong here, rather than in GUI callbacks.

`records.cpp` owns shared record prototypes and contextual factories (`record.defaults`, or `append` with `prototype` and `context`). `seeds.cpp` owns optional-block and expression defaults. `structure.cpp`, `tree_actions.cpp`, `layout_actions.cpp`, `ship.cpp` and `ship_graph.cpp` own their semantic transformations.

Content-specific panels must use shared descriptors/groups, production queries and registered actions. The architecture gate rejects raw data-entry widgets, inline defaults (including dynamic keys), inline record constructors and GUI-declared fallback values. `shell/view-bindings.json` is the audited exception list for transient presentation state and pending action arguments; it must never contain an authored-data binding. The generic renderer and structured JSON editor remain presentation adapters. Adding a registered authoring action or descriptor automatically exposes it to CLI discovery and dispatch.

The core may depend on content documents/compilers and public definition types. It must not link graphics, ImGui, simulation or Flecs. GUI mutations use a disposable presentation buffer; publication and undo belong to `WorkshopAuthoring`. Semantic helpers publish successful candidates while preserving references to unchanged JSON children used by the GUI.

See [the CLI contract](../../../docs/workshop-cli.md) for workflows and extension rules. Synthetic integration and architecture checks protect these boundaries; graphical acceptance is separate.
