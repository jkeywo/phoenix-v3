# Agent-contract compliance follow-up

Implementation in progress, 2 November 2026. This record separates delivered changes and executed checks from the remaining typed-decision migration.

## Local clinical workflows

Medical team fittings can declare `local_workflow`, either an ordinary reusable tree reference or an embedded tree. The tree uses `context: "team_workflow"`, declares its supported `order_ids`, and runs through the existing evaluator. Its vocabulary contains `arrived`, `order_complete`, `order.<id>.eligible` and argument-free `start.<id>` actions. Compilation checks that each referenced order belongs to this fitted team, targets patients and has the required Medical capability. No Controller command or world-query vocabulary is supplied.

The workflow represents continuing local care authorised by the fitting's initial compartment, an admitted compartment assignment or an explicit clinical order. An explicit order runs first; the workflow can choose follow-up orders afterward, including when the human owns detailed clinical controls. A replacement order resets the workflow. Cancellation removes the continuing scope. Travel, clinical triage, supplies, facilities and execution of the selected order remain native mechanisms. Patient work is retained across interruptions. An explicit order's named patient remains its established triage priority; subsequent workflow orders are confined to that admitted patient and cannot widen the follow-up to neighbours. The saved workflow continuation distinguishes the explicit order from follow-up execution.

The clinical workflow cannot change its authorised compartment or borrow another team's orders. A fitting without a workflow can still execute an explicit single order; automatic Medical compartment assignment requires a workflow. Existing Console layouts and complexity allocations are retained.

Workflow scope and continuation are checkpointed under World/session schema 52. Incompatible saves are refused without rewriting their files. Native and browser session readers must use the same schema.

Workshop exposes the binding and declared order names through its existing tree editor and shared authoring backend. Context defaults and vocabulary are shared with the CLI. The production loader uses normal inheritance, local-tree identity and referenced-tree resolution.

## Remaining work

The full typed Controller snapshot migration is not complete. Domain decision preparation still consumes `Observation::gameplay`, named telemetry and string-keyed candidate facts. Completing the agreed plan requires direct typed producers, migrated domain consumers and arbitration, explicit serialization-only JSON boundaries, removal of transitional paths, and the final cross-platform and manual acceptance gates.

## Verification

The native Medical journey suite passed after the workflow changes, including explicit-order follow-up, delegation, named-patient restrictions, replacement/cancellation, unavailable facilities, away reservation, handover and interrupted restore. The native Workshop authoring suite passed shared factory/vocabulary, inherited/reference/local binding, GUI-helper/CLI equivalence and source save/export/reload checks. Module ownership and shared-authoring architecture checks passed. The pack policies use named reusable `base.workflow.medical-local-care` and `test.workflow.medical-local-care` trees; final production-pack verification is still pending after that content change.

The integrated native Controller suite passed with the current typed sources. Full native, cross-platform continuation and representative graphical checks remain pending; a successful compiler invocation does not establish these acceptance requirements.
