# Effect transactions

`phoenix.effects.transactions` owns the move-only prepared effect quote, captured payment revisions and shared commit sequence. Preparation changes copies only. Commit checks native context and freshness, then commits stocks, consumes the quote, advances its transient epoch and installs prepared native state in that order.

`phoenix.effects.preparation` owns operation dispatch, native candidate copies, stock aliases, thermal preparation and fractional property operations. It accepts immutable definition views and explicit native queries. Native adapters supply source/target eligibility and domain grant policies; the module receives no World reference.

`phoenix.effects.native` resolves actual equipment/officer/compartment owners and canonical stock addresses against their scoped native definitions. Physical mechanisms retain their authoritative state.

Start with `transactions.cppm`, `preparation.cppm` and their implementation files. `PreparedEffectGroup` remains move-only because stock changes can retain addresses into its candidate networks; moving its maps preserves those addresses. Native cost resolution, lifecycle effects and final writeback are the remaining contextual integration points.

The migration ledger records verified integration checkpoints. Production-path store-effects, rules, abilities and process cases exercise atomic failure, once-only commit and restored continuation.
