# Shield migration — implementation and verification

23 October 2026

The [shield migration plan](shield-migration-plan.md) implements the first slice of the [shared gameplay engine](gameplay-engine-design.md). This record distinguishes implemented behaviour, automated verification and remaining manually triggered evidence.

## Implemented behaviour

- Shared `effects` catalogue with dependency/override resolution, inheritance, strict typed compilation, Workshop editing and dependency/export integration.
- Ship-owned named shield contributions. Fitted equipment retains power/control bindings, with one funded budget partitioned across its contributions.
- Persistent reserve, warmup and fractional accounting through enablement, funding changes, refresh and save/restore; complete effect-group validation on restore.
- Production scenario apply, refresh and remove actions with stable handles, explicit finite duration and replay-safe confirmation receipts.
- Explicit `shield_feed` observations, independent of command ownership. Temporary grants are read-only unless a supported control binding exists. Sensors and displacement checks read the same native shield component.
- Additive recharge, sustain, discharge and protection property modifiers, including the existing officer/fault bridge. Fixed grant parameters remain fixed in this slice.
- Equal-layer sharing, native bypass accounting and contribution-specific pure pre-damage reduction/negation. Unreached layers receive no hooks.
- World-owned, next-tick reactions with captured hit/contribution results, reusable grant apply/remove operations and secondary damage. Group validation and staged payment prevent partial grants or spending on failure. Bounded persisted execution results explain failures.
- Maintained shield definitions extracted to shared effects. Production inline shield compilation is removed; synthetic builders translate their own shorthand before using the production loader.
- Save schema 18 for contributions, pending reactions, diagnostics and execution identities; older world/session schemas are refused consistently.

This slice supports fixed scalar pre-hooks and typed finite reaction operations. Captured event results are available for continuation and inspection; general authored event-expression evaluation and the wider gameplay engine remain later work.

## Agent review and corrections

An agent reviewed the initial implementation against the plan, and another agent independently reviewed the completed reaction execution. The review identified and drove corrections to:

1. Pre-damage transformation taking place after reserve limiting. Presented damage is now transformed before limiting actual absorption; reduction is bounded to prevent reserve underflow.
2. Incomplete or duplicate equipment effect groups accepted by restore. Complete named rosters and consistent group metadata are now required.
3. Temporary contributions being visible without an explicit feed. Observation bindings are now authored and retain human/AI parity.
4. Reactions being owned by a component that could disappear with its granting entity. Queues now belong to the world and retain source bookkeeping independently.
5. Missing grouped reaction operations and atomic paid execution. Apply/remove/damage groups now validate and stage their required state and payment together.
6. Missing beam source lineage. Sustained beam hit records now retain their originating ship for reaction targets and diagnostics.
7. Ambiguous or ignored operation fields. Compilation rejects unsupported fields and requires grant changes to precede damage inside a prepared group.
8. Missing fractional recharge and shared-payer handling. Millistrength remainders persist, and sustain is reserved before recharge budgets are partitioned.

## Automated evidence

All 37 native system cases passed, along with the native build check and resolved-content verification. Focused combat, scenario World, sensors and away cases passed again after review fixes; the final sensor and away run also includes scenario-created shields on ships with no fitted shield equipment.

The real-network suite passed 11 of 12 cases on its final full run, including schema-18 playable-fleet checkpoint recovery. The remaining missing-sidecar lobby case timed out during a concurrent build and passed on an isolated rerun. This is recorded as a retry, rather than a clean full-gate pass. The earlier recovery failure was a stale schema-17 assertion in the manual network probe, corrected to 18.

The complete WebAssembly automatic gate passed, including content/transfer checks, system integration and all existing native/WASM continuation comparisons. After the final test additions, rebuilt WebAssembly sensor, away and combat cases also passed. The extended combat trace matches native exactly for both pending paid and unaffordable reaction groups, their restored execution, payment and resulting temporary grants.

The synthetic coverage includes template overrides/local specialisation, invalid definitions, duplicate JSON keys, contribution lifecycle, multi-arc funding, incomplete-save refusal, independent scenario grants, permitted observations, pre-negation and reached-layer behaviour, paid/unaffordable reaction groups and pending-reaction continuation.

## Simplification follow-up

The architecture review led to three changes:

1. Equipment, scenario actions and staged reactions now share one grant constructor. Compiled effects and runtime contributions carry shield-specific definitions instead of copies of the general combat-system definition; duration lives alongside the compiled contributions.
2. The compiler turns `post_damage` into the final ordinary reaction group. Authoring syntax and schema-18 group indices remain unchanged, and queueing, restoration and execution no longer need a shorthand-specific branch.
3. Ordinary resource operation and late exact payment share debit and fractional heat arithmetic. Affordability, tick ordering, cooling, stress and thermal-stop checks retain their separate existing contracts.

A fresh agent reviewed these changes for correctness, duplication and unnecessary abstractions and found no actionable gaps. Seven affected suites passed on both native and WebAssembly: content, gameplay foundations, flight, combat, scenario World, sensors and away. Native/WebAssembly combat continuation traces match exactly, including pending paid and unaffordable reaction groups. New synthetic checks cover mixed authored/shorthand reaction ordering across restore and equivalent fractional heat accounting for ordinary and late payments, including unaffordable-payment atomicity. This follow-up reran the affected suites rather than the complete network/platform gate; no manual smoke was performed.

## Manual evidence and limits

No graphical, physical-device, screen-reader or performance smoke run was performed as part of this implementation pass. Existing player presentations consume the common observations, with unique contribution semantic identities; Workshop exposes the authored effect definitions and references. Actual graphical/accessibility/platform acceptance remains separately and manually triggered under the [testing policy](testing.md).

The automated fixtures are independent of shipped ships and balance packs. No production-content golden or shipped tuning assertion is introduced.
