# Effect lifecycle

`phoenix.effects.events` owns the pending rule/damage queue, stable identities, bounded diagnostic journal and draining capacity. It freezes all due evaluations before executing them in identity order. Native adapters supply fact evaluation, physical execution and final reconciliation.

`phoenix.effects.clocks` owns derived clock addresses and capacity bookkeeping. Persisted clocks remain on native `RuleRuntime` and `ActiveRuleEffect` records; the store retains no ECS pointers. Its periodic-frontier provider advances due clocks and returns typed pending events. The periodic-frontier provider passed standalone compilation; its native wrapper is activated for the next integration checkpoint. Native integration refreshes eligibility first, then publishes all returned events only after every due clock advances.

`phoenix.effects.active_rules` owns the active-condition query, stable-ID index, ordered handle cache and next identity. `ActiveRuleStore` reconciles snapshots, creates and removes actual condition entities, and retires expired or unavailable conditions. It uses `RuleClockStore` directly for derived clock indexing and capacity; native actor-specific ports resolve physical targets, withdraw grants and publish rate/transaction changes. Its standalone compilation passed and native bindings are activated for the next integration checkpoint.

The clock owner also freezes all source evaluations and seen-key membership before any retirement or physical commit. Maintained retarget and lease output decisions use the existing typed `RuleApplications` evaluator; native binding matching remains an actor-specific query.

`phoenix.effects.grants` owns periodic grant transitions. `phoenix.effects.continuation_validation` checks restored contexts, captures, credits and clock invariants against explicit definitions and native identity queries. Dynamic native fact capture and cross-domain eligibility refresh remain native integration responsibilities.

The World serializer obtains const queue snapshots and restores typed validated state; queue persistence retains the existing next-job, pending and result representation. No queue state is exposed mutably. Native checkpoint 2 passed all 48 system/architecture checks and all 23 fresh continuation traces matched the baseline exactly. Browser validation is pending. Relevant production-path cases include rules, combat, processes and save/restore continuation.
