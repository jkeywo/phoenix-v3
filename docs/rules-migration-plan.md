# Shared rules, regions and reaction consolidation

26 October 2026. Approved implementation in progress, based on `b907e3a`. This is the sixth slice of the [shared gameplay engine](gameplay-engine-design.md). It follows [shared abilities](ability-migration-evidence.md); this plan does not establish delivery or acceptance.

## Approved result

Reusable rules combine triggers, typed conditions, targets, costs and effects. Regions, equipment, officers and active effects bind common catalogue templates. Consolidate shield reactions, hit/destruction payload bindings and periodic effects; complete pre/post hooks around native shield, compartment and system damage. Preserve native physics, damage resolution, resource distribution, sensing and field geometry.

Include bounded typed value expressions, maintained contributions and independently funded ongoing rules. Defer custom mutable properties and the wider machinery-process migration. Reuse existing scalar types, compiled evaluation, stock transactions, prepared allocations and scheduled phases; do not introduce a separate DSL per source, parallel resource balances, or a recursive event scheduler.

## Execution contracts

- Compile rule definitions and bindings once through the mod-extensible catalogue. Context distinguishes owner, original source, event subject, selected targets and explicit cost payers. Trusted authored content retains authoritative access; Console/AI publication remains scoped.
- Support Boolean composition, comparisons and checked integer arithmetic including ratios, min/max/clamp. Validate units, supported facts, target capabilities and trigger-field availability at compilation. Invalid arithmetic or failed required costs/operations leaves the execution uncommitted.
- Freeze context for its scheduled phase. Capture source-dependent projectile values and bindings at launch; add target and actual hit facts at impact. In-flight work continues independently of source destruction or later mode changes.
- Maintain owned contributions once and update only supported live values. Withdrawal does not reverse independent consequences. Distinguish instant, periodic and maintained work so reevaluation cannot refill reserves or repeat one-shot effects.
- Ongoing rules use stable named consumers in existing prepared funding/accounting, without fake equipment. Preserve exact resource rates/remainders. Proportional output requires a scalable operation; pause preserves state without unpaid output; stop withdraws grants and requires a fresh eligibility transition before automatic restart.
- Pre-damage rules may pay costs and perform supported operations, and scale/cancel pending damage. Prepare without world mutation; allocation may reuse quotes but cannot replay rule evaluation/payment/effects. Resolve payment conflicts before final sharing; commit affected pre-work once. Failed rules leave the original hit unchanged. Actual absorption/infliction belongs to post context. Native contribution-topology changes apply to subsequent hits; secondary damage stays scheduled.
- Preserve existing bypass routes, penetration, layer/equal-sharing arithmetic and payload filters. Unreached shield layers have no hooks. Remove superseded authored execution paths when common replacements work.
- Reuse existing ECS phase order and shared queue handling. Work created while draining a phase cannot recursively execute in that same drain; stable logical identities govern ordering and receipts.

## Regions and maintained sources

Reuse circle/rectangle/annulus geometry and native spatial/movement queries. Add entry, exit, periodic occupancy and maintained membership rules. Enumerate swept intervals in movement order, including complete crossings between samples. Maintained membership follows sampled position; spawn-inside emits one entry, restore does not replay it, and despawn retires membership without inventing movement exit.

Preserve nebula attenuation/visuals and asteroid generation. Existing scenario actors, including carried craft, participate; new geometry, moving auras and projectile membership are deferred. Synthetic fixtures prove new hazards without changing shipped tuning.

Officer buff eligibility/contribution maintenance moves to shared maintained-rule handling; assignment, travel, qualification and additive property semantics remain native. Equipment and active effects use the same typed source bindings and retain their native state/lifetime owners.

## Delivery sequence

1. Compiled rules/expressions/context and common prepared effect execution.
2. Maintained source execution, named funding, shortage contracts and continuation.
3. Damage adapters, shield/payload/periodic migration and launch-captured context.
4. Region rules, maintained officer/equipment integration, Workshop/export, diagnostics and permitted projections.
5. Independent correctness review, architecture audit, fixes, independent re-review and final integration gates.

Persist identities, clocks, membership, funding remainders, captured context and pending executions without duplicating native reserves/lifetimes. Migrate maintained content and fixtures together. World/portable session schema advances from 26 to 27; incompatible saves require a new game. Retired authoring fields fail with actionable diagnostics rather than retaining a permanent compatibility runtime.

Commit verified logical steps. Record delivery and actual evidence separately, including incomplete requirements and manual acceptance boundaries.

## Validation

Synthetic production-path fixtures cover template overrides; typed/invalid expressions; atomic failures; overlapping maintained sources; independent consequences; own funding competing with native consumers; shortages/remainders/restore; swept region crossings and annuli; initial/restored membership; every damage-hook location; cancellation and failed costs; weak/equal-layer shields with exactly-once pre-work; captured launch values after source changes/removal; periodic/destruction delivery; and exact interrupted continuation.

Run affected native checks during implementation, then complete native/threaded WASM gates, content verification, formatting and continuation comparisons. Run focused ordinary WASM rule/reaction checks and production preflight too. Keep live-network checks separate from expensive compilation. GUI/accessibility/device/performance smoke acceptance remains explicitly separate.
