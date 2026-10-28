# Document workflow

`phoenix.workshop.workflow` owns one `WorkshopWorkspace`: editable document,
history, import/save queues, file locations, selection and editor collaborators.
Its interface is `workflow.cppm`; `state.hpp` is private to this named module.
Read `editor.cpp` for composition, `io.cpp` for asynchronous document operations,
and `appearance.cpp`/`consoles.cpp` for authoring inspectors. `diagnostics.cpp`
retains the existing explicitly invoked smoke stages.

`WorkshopEnvironment` supplies installed-library/session access, status, a tool
frame and navigation/preference persistence callbacks. These callbacks are
provided by application composition; no editor receives the whole Application.
Map, layout, tree, material preview, gameplay inspectors and disposable tests own
their own transient state. Draft history continues through WorkshopDocument.

Close/reset drains input/save state and clears GPU caches before backend teardown.
There is no new saved workflow format. Production-loader content/menu and
Workshop-host cases cover document/export/test contracts; visual editing and
smoke-stage execution remain separate manual checks.
