# Controller state and continuation

`phoenix.control.controllers` owns typed Console/Controller command sections, AI continuations, accepted request state and action persistence. `controllers.cppm` is the interface; `controllers.cpp` implements profile selection and action checkpoint validation without Flecs or World access.

Definitions come from `phoenix.content.controllers.types`. State-tree continuation encoding uses the generic evaluator. Native actuation and command admission remain separate. Persist only actual continuation; compiled bindings and transient action writes are rebuilt. Relevant integration: Controller runtime, session recovery and native/WASM continuation.

`phoenix.control.templates` is a separate assembly compiler in this folder. It builds immutable Controller command and brain/binding templates from a catalogue and resolved layout; the record/runtime target does not acquire content compiler dependencies. World installs copies into entities in the original creation order.

Compiled brains share immutable observation-name indexes, numeric candidate slots and objective-record field addresses for the same tree template. Template loading also resolves adapter sources, action indexes, report schema/payload slots and action write-channel endpoints; ordinary arbitration does not scan action programs or resolve authored field names. This metadata contains schema addresses, not observed values or private memory. Copies retain independent continuation. Decision adapters prepare candidate categories lazily within one evaluation of frozen permitted inputs; they retain no references across evaluations.

Provider deviation suppression keeps bounded typed order/reason receipts alongside diagnostic history. They compare admitted order epochs, priorities and alert state without decoding diagnostic JSON during execution. Restore reconstructs these receipts once from the unchanged history checkpoint boundary.

Objective evaluation records transient typed diagnostic rows. The Console and decision provider use the same encoder when projecting them, including the existing optional evidence field.

Controller views and objective diagnostics reserve their output arrays and construct JSON objects directly. The view still evaluates action availability through the shared state-tree evaluator.
