# Process execution state

`phoenix.effects.processes` owns process continuation and the state transitions used by machinery and finite work: prepaid-work arithmetic, grant identities, funding pause/resume/stop transitions and work receipts. Native targets retain physical progress and the caller retains atomic transaction ownership.

The folder has four compiled owners. `processes.cppm` defines persisted process state and transition helpers. `machinery.cppm` owns the transient `ProcessMachineryBook`, including quoted rates, rejected identities and current-run funding caps. `finite_work.cppm` owns finite-work quotation, commit validation, credits and receipts. `validation.cppm` checks restored process episodes against immutable definitions and native grant bindings.

Physical mechanisms remain in their gameplay families. `phoenix.gameplay.machinery` reads actual fitting, processor, FTL and personnel state to decide machinery availability. `phoenix.gameplay.flight.work` owns candidate basic-flight simulation and physical work calculation. `phoenix.resources.processing` owns processor component state. Native adapters bind these owners to the World schedule and retain cross-domain admission and completion ordering.

Inputs are explicit definitions, native-owned state and fixed tick values; these owners do not read World. Existing serialization, including the schema-31 completion alias, is preserved. The final native and ordinary/threaded WASM project gates passed, including continuation comparisons; see the [verification record](../../../docs/modularisation-evidence.md). Relevant production-path system cases include processes, rules, store effects, repair/Medical, craft and FTL.
