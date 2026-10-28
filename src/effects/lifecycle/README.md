# Effect lifecycle events

`phoenix.effects.events` owns the pending rule/damage queue, stable identities, bounded diagnostic journal and draining capacity. It freezes all due evaluations before executing them in identity order. Native adapters supply only fact evaluation, physical execution and final reconciliation callbacks.

The World serializer obtains const queue snapshots and restores typed validated state; queue persistence keeps the existing next-job, pending and result representation. No queue state is exposed mutably. Relevant production-path system cases are rules, combat, processes and save/restore continuation.
