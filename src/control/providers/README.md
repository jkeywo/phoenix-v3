# Controller decision providers

`phoenix.control.providers` evaluates authored action availability and candidate descriptions from Controller definitions and permitted typed observations. `providers.cppm` is the interface; `providers.cpp` owns evaluation and candidate formatting. It has no physical World lookup or transport dependency.

Provider protocol/networking is separate. The pure module also builds bounded decision snapshots and their comparison basis from explicit permitted evidence and Controller/blackboard state. World captures those inputs and retains authoritative decision admission and fallback integration in `src/world/command-routing/control_controller_models.cpp`. Candidate evaluation creates temporary tree instances and never mutates live physical or command state. Relevant integration: Controller runtime and inference adapters.

`phoenix.control.inference_host` is a separate higher-level module in this folder. Its PIMPL owns coalesced pending/sent requests and decision basis through `set_connection`, `status`, `reset` and `poll`. Only local/network hosts link its World/inference dependencies; simulation uses the pure candidate helpers without importing host scheduling.

Controllers with compiled objective bindings expose their permitted candidate eligibility, integer score breakdown and selected objective to providers even when they have no legacy decision candidates. Such providers may continue or defer; shared arbitration still selects and advances actions on each tick. They cannot inject an alternate action or bypass Reaction/Captain/tier ordering. Provider changes and fallback preserve the scored execution continuation and completed physical work.
