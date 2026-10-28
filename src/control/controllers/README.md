# Controller state and continuation

`phoenix.control.controllers` owns typed Console/Controller command sections, AI continuations, accepted request state and action persistence. `controllers.cppm` is the interface; `controllers.cpp` implements profile selection and action checkpoint validation without Flecs or World access.

Definitions come from `phoenix.content.controllers.types`. State-tree continuation encoding uses the generic evaluator. Native actuation and command admission remain separate. Persist only actual continuation; compiled bindings and transient action writes are rebuilt. Relevant integration: Controller runtime, session recovery and native/WASM continuation.

`phoenix.control.templates` is a separate assembly compiler in this folder. It builds immutable Controller command and brain/binding templates from a catalogue and resolved layout; the record/runtime target does not acquire content compiler dependencies. World installs copies into entities in the original creation order.
