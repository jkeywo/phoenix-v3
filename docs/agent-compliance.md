# Agent-contract compliance follow-up

Implementation and integration verification, 2 November 2026. This record separates the implemented contracts from executed checks and outstanding acceptance evidence.

## Local clinical workflows

Medical team fittings can declare `local_workflow`, either an ordinary reusable tree reference or an embedded tree. The tree uses `context: "team_workflow"`, declares its supported `order_ids`, and runs through the existing evaluator. Its vocabulary contains `arrived`, `order_complete`, admitted `assignment.compartment`/`assignment.patient`, its own `order.active`/`order.id`, `order.<id>.eligible` and argument-free `start.<id>` actions. Compilation checks that each referenced order belongs to this fitted team, targets patients and has the required Medical capability. No Controller command or world-query vocabulary is supplied.

The workflow represents continuing local care authorised by the fitting's initial compartment, an admitted compartment assignment or an explicit clinical order. An explicit order runs first; the workflow can choose follow-up orders afterward, including when the human owns detailed clinical controls. A replacement order resets the workflow. Cancellation removes the continuing scope. Travel, clinical triage, supplies, facilities and execution of the selected order remain native mechanisms. Patient work is retained across interruptions. An explicit order's named patient remains its established triage priority; subsequent workflow orders are confined to that admitted patient and cannot widen the follow-up to neighbours. The saved workflow continuation distinguishes the explicit order from follow-up execution. First evaluation waits for the explicit order to finish and for arrival at the admitted compartment. Once initialised, the workflow continues ticking during its own physical orders, escorts and travel, so its ordinary tree timers and completion waits include that work. New starts are refused while an order is active or the team is outside the admitted compartment; local eligibility outside that compartment is false. The frozen scope and active-order facts do not add movement or Controller authority.

The clinical workflow cannot change its authorised compartment or borrow another team's orders. Within one evaluation, only the first eligible start is admitted; later starts are refused through ordinary evaluator feedback rather than replacing staged work. A fitting without a workflow can still execute an explicit single order; automatic Medical compartment assignment requires a workflow. Existing Console layouts and complexity allocations are retained.

Workflow scope and continuation are checkpointed under World/session schema 52. Incompatible saves are refused without rewriting their files. Native and browser session readers must use the same schema.

Workshop exposes the binding and declared order names through its existing tree editor and shared authoring backend. Context defaults and vocabulary are shared with the CLI. The production loader uses normal inheritance, local-tree identity and referenced-tree resolution.

## Typed Controller decisions

`phoenix.knowledge.decision` composes portable domain records owned under Knowledge projections. Native producers populate them directly from permitted domain feeds. `ConsoleDecisionFrame`, fallback capture, action availability, coordination evidence, retained inspections and reporting consume immutable `DecisionObservation` records, without `Observation::gameplay` or named telemetry maps.

Controller loading resolves telemetry fields, record/schema addresses, workflow slots, candidate fields, report payload slots, capability endpoints, adapter sources and action references. Arbitration uses enum/indexed facts and typed candidate metadata. Textual values and stable equipment, policy and request identities remain actual data; they do not become authored field-path interpreters.

Contact, inspection and sensor payloads are decoded at evidence admission or restore. The immutable cache retains malformed latest-stream markers, withdrawal, provenance, freshness and recipient scope. Equal-version publication merges invalidate their transient cache generation; frozen snapshots retain the earlier immutable payload. Controller filtering removes private records and their cached payloads together.

Provider candidate generation retains typed operations and arguments through admission. Provider offers, diagnostics, messages, presentation and checkpoints keep their existing JSON formats at explicit serialization boundaries. Reporting deduplication uses typed private receipts rebuilt once from saved history. The unused JSON decision helpers and the old mixed presentation/decision capture modes have been removed. The documented navigation lookout exception is unchanged.

Native producers check actual runtime components rather than assuming source definition metadata implies a fitted mechanism or a Bridge blackboard. Passive and streamed bodies without those components contribute no invented authority or detailed scanner state. Ordinary permitted contact detection retains its existing path.

## Verification

| Gate                                                     | Executed result                                    |
| -------------------------------------------------------- | -------------------------------------------------- |
| Native build and complete CTest suite                    | Passed, 53/53 cases                                |
| Native executable build check                            | Passed                                             |
| Launcher/trace/deployment JavaScript cases               | Passed, 45/45 cases                                |
| Native signalling, WSS, session and provider integration | Passed, 12/12 cases                                |
| Ordinary Release WASM affected journeys                  | Passed, 29/29 cases; final no-drift rebuild passed |
| Threaded Release WASM affected journeys                  | Passed, 29/29 cases; final no-drift rebuild passed |
| Fresh native/threaded continuation comparisons           | Passed, 23/23 pairs                                |
| Fresh native/ordinary continuation comparisons           | Passed, 23/23 pairs                                |
| Module ownership                                         | Passed, zero violations                            |
| Shared-authoring architecture                            | Passed, 517 field descriptions                     |

The final World execution binaries were rebuilt with the three native component-presence guards. An initial full native gate passed 50 of 53 cases and exposed access violations in scenario-world, combat and conversations. Native fault data identified a copied objective-priority set through an absent Bridge blackboard. The corrected producer passes those focused regressions and the subsequent complete native suite.

Native build and CTest evidence is in `build/compliance-native-guard-final-check.log`. After the 53-case suite passed, its redundant serial continuation tail was intentionally stopped. The executable build check and network-service gate passed separately; the same 23 native binaries and `--trace` arguments were captured in a three-process test-only pool under `build/compliance-continuations/native-guards`. Threaded traces are under `build/compliance-continuations/threaded`; ordinary traces are under `build/compliance-continuations/ordinary`. No production scheduler or gameplay thread was added.

WASM runtime receipts distinguish ordinary full-suite runs from audited full-suite-with-trace runs. Twenty threaded cases and six ordinary cases supplied both full runtime assertions and fresh trace output in one execution. Combat, conversations and Away retain separate normal and trace executions because their trace modes return before some normal assertions. Reused artifacts are exact successful captured outputs; these are fresh comparison records, not authored-content goldens.

The native Medical journey suite passed after the workflow changes, including explicit-order follow-up, delegation, named-patient restrictions, replacement/cancellation, unavailable facilities, away reservation, handover and interrupted restore. It also covers active-order facts/timers and refusal of a second start in one evaluation. The native Workshop authoring suite passed shared factory/vocabulary, inherited/reference/local binding, GUI-helper/CLI equivalence and source save/export/reload checks. Module ownership and shared-authoring architecture checks passed. The pack policies use named reusable `base.workflow.medical-local-care` and `test.workflow.medical-local-care` trees. Production verification passed for maintained `base`, `test-missions`, `test` and `test-3d` packs.

Temporary differential instrumentation compared direct typed captures and bindings against the prior JSON projection path on synthetic Controller, Medical, sensor, ability, process and craft journeys. All six suites passed: 22,232 fact/arbitration evaluations, 717,648 candidate rows and 8,025 selected choices, with zero differences. This compares capture/binding/arbitration paths using the current typed consumers; it is not an archived production-content golden. The harness was removed and the four production files restored to their exact pre-harness hashes before final cleanup.

## Outstanding acceptance evidence

Built-in native captures were visually inspected for the authored support Console and the Workshop loaded with current test data. These are narrow render checks. The interactive Windows helper repeatedly timed out on `list_windows`, including after its documented reset, so the post-fix clinical/workflow editing journey remains unverified. The existing Controller Workshop graphical smoke also requires an obsolete schema-1 fixture, which the current shared-widget loader correctly refuses; that diagnostic needs migration before it supplies acceptance evidence. Physical devices, screen readers, graphical browser journeys, six-display operation and performance targets remain unverified.
