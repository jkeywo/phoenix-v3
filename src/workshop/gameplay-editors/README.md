# Gameplay definition inspectors

`phoenix.workshop.gameplay` exposes GameplayEditors in `gameplay.cppm`. It owns
authoring selection and resolved-fitting snapshots; it borrows WorkshopDocument
and presentation flags. There is no Application, renderer or authoritative runtime
in this interface.

Read the domain implementation needed: `equipment.cpp`, `combat.cpp`,
`operations.cpp`, `resources.cpp`, `effects.cpp`, `knowledge.cpp`, `scenarios.cpp`
or `consoles.cpp`. `state.hpp` contains private declarations shared only by this
named module. Tree selection/vocabulary belongs to TreeEditor, and requests and
feedback belong to WorkshopTestClient. Definition compilation/validation remains
the production content implementation.

No editor state is added to save formats. Existing production-loader content
verification and disposable-host system cases protect accepted definitions;
interactive inspector editing remains a manual smoke journey.
