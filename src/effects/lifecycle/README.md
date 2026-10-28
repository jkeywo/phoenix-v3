# Effect lifecycle

`phoenix.effects.events` owns the pending rule/damage queue, stable identities, bounded diagnostic journal and draining capacity. It freezes all due evaluations before executing them in identity order. Native adapters supply fact evaluation, physical execution and final reconciliation.

`phoenix.effects.clocks` owns derived clock addresses and capacity bookkeeping. Persisted clocks remain on native `RuleRuntime` and `ActiveRuleEffect` records; the store retains no ECS pointers. Its periodic-frontier provider advances due clocks and returns typed pending events. The periodic-frontier provider passed standalone compilation; its native wrapper is activated for the next integration checkpoint. Native integration refreshes eligibility first, then publishes all returned events only after every due clock advances.

`phoenix.effects.grants` owns periodic grant transitions. `phoenix.effects.continuation_validation` checks restored contexts, captures, credits and clock invariants against explicit definitions and native identity queries. Dynamic native fact capture and cross-domain eligibility refresh remain native integration responsibilities.

The World serializer obtains const queue snapshots and restores typed validated state; queue persistence retains the existing next-job, pending and result representation. No queue state is exposed mutably. Native checkpoint 2 passed all 48 system/architecture checks and all 23 fresh continuation traces matched the baseline exactly. Browser validation is pending. Relevant production-path cases include rules, combat, processes and save/restore continuation.
