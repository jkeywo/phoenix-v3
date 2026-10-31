# Headless Workshop authoring

Read `core.cppm` for the shared session, read-only published document and candidate-operation interfaces. `operations.cpp` registers document operations and descriptions together; `core.cpp` owns request dispatch, transactions and history. `filesystem.cpp` is native-only persistence.

`fields.json` owns fixed standard field descriptions/defaults/choices. CMake embeds it as a byte array so discovery works without locating resources or opening a window. `records.cpp` owns shared record prototypes. The GUI uses these through its standard authoring controls. `structure.cpp` owns layout drop/wrap/move and state-rename behavior shared with canvas editors.

The core may depend on content documents/compilers and public definition types. It must not link graphics, ImGui, simulation or Flecs. The GUI's mutable document is a presentation buffer; published state and undo belong to `WorkshopAuthoring`. CLI requests edit disposable copies and publish only on success.

See [the CLI contract](../../../docs/workshop-cli.md) for workflows and extension rules. Native system integration and architecture checks protect these boundaries; they do not establish graphical acceptance.
