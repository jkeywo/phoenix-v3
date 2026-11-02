# Agent-contract compliance follow-up

Implementation and integration verification, 2 November 2026. This record separates the implemented contracts from executed checks and outstanding acceptance evidence.

## Local clinical workflows

Medical team fittings can declare `local_workflow`, either an ordinary reusable tree reference or an embedded tree. The tree uses `context: "team_workflow"`, declares its supported `order_ids`, and runs through the existing evaluator. Its vocabulary contains `arrived`, `order_complete`, `order.<id>.eligible` and argument-free `start.<id>` actions. Compilation checks that each referenced order belongs to this fitted team, targets patients and has the required Medical capability. No Controller command or world-query vocabulary is supplied.

The workflow represents continuing local care authorised by the fitting's initial compartment, an admitted compartment assignment or an explicit clinical order. An explicit order runs first; the workflow can choose follow-up orders afterward, including when the human owns detailed clinical controls. A replacement order resets the workflow. Cancellation removes the continuing scope. Travel, clinical triage, supplies, facilities and execution of the selected order remain native mechanisms. Patient work is retained across interruptions. An explicit order's named patient remains its established triage priority; subsequent workflow orders are confined to that admitted patient and cannot widen the follow-up to neighbours. The saved workflow continuation distinguishes the explicit order from follow-up execution.

The clinical workflow cannot change its authorised compartment or borrow another team's orders. A fitting without a workflow can still execute an explicit single order; automatic Medical compartment assignment requires a workflow. Existing Console layouts and complexity allocations are retained.

Workflow scope and continuation are checkpointed under World/session schema 52. Incompatible saves are refused without rewriting their files. Native and browser session readers must use the same schema.

Workshop exposes the binding and declared order names through its existing tree editor and shared authoring backend. Context defaults and vocabulary are shared with the CLI. The production loader uses normal inheritance, local-tree identity and referenced-tree resolution.

## Typed Controller decisions

`phoenix.knowledge.decision` composes portable domain records owned under Knowledge projections. Native producers populate them directly from permitted domain feeds. `ConsoleDecisionFrame`, fallback capture, action availability, coordination evidence, retained inspections and reporting consume immutable `DecisionObservation` records, without `Observation::gameplay` or named telemetry maps.

Controller loading resolves telemetry fields, record/schema addresses, workflow slots, candidate fields, report payload slots, capability endpoints, adapter sources and action references. Arbitration uses enum/indexed facts and typed candidate metadata. Textual values and stable equipment, policy and request identities remain actual data; they do not become authored field-path interpreters.

Contact, inspection and sensor payloads are decoded at evidence admission or restore. The immutable cache retains malformed latest-stream markers, withdrawal, provenance, freshness and recipient scope. Equal-version publication merges invalidate their transient cache generation; frozen snapshots retain the earlier immutable payload. Controller filtering removes private records and their cached payloads together.

Provider candidate generation retains typed operations and arguments through admission. Provider offers, diagnostics, messages, presentation and checkpoints keep their existing JSON formats at explicit serialization boundaries. Reporting deduplication uses typed private receipts rebuilt once from saved history. The unused JSON decision helpers and the old mixed presentation/decision capture modes have been removed. The documented navigation lookout exception is unchanged.

## Remaining acceptance work

The final native build/check and full suite, affected ordinary/threaded WASM journeys, fresh continuation comparisons, maintained-pack verification and representative Medical/Workshop visual journeys are still in progress. Physical device, screen-reader, six-display and performance evidence must remain separate from headless checks.

## Verification

The native Medical journey suite passed after the workflow changes, including explicit-order follow-up, delegation, named-patient restrictions, replacement/cancellation, unavailable facilities, away reservation, handover and interrupted restore. The native Workshop authoring suite passed shared factory/vocabulary, inherited/reference/local binding, GUI-helper/CLI equivalence and source save/export/reload checks. Module ownership and shared-authoring architecture checks passed. The pack policies use named reusable `base.workflow.medical-local-care` and `test.workflow.medical-local-care` trees; final production-pack verification is still pending after that content change.

The integrated native Controller suite passed with the current typed sources. Full native, cross-platform continuation and representative graphical checks remain pending; a successful compiler invocation does not establish these acceptance requirements.

Temporary differential instrumentation compared direct typed captures and bindings against the prior JSON projection path on synthetic Controller, Medical, sensor, ability, process and craft journeys. All six suites passed: 22,232 fact/arbitration evaluations, 717,648 candidate rows and 8,025 selected choices, with zero differences. This compares capture/binding/arbitration paths using the current typed consumers; it is not an archived production-content golden. The harness was removed and the four production files restored to their exact pre-harness hashes before final cleanup.
