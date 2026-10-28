# Ability execution

`phoenix.effects.abilities` owns the authoritative ability grant component, continuation fields, recovery keys and reservation accounting. `phoenix.effects.ability_validation` validates restored grants and bindings without changing their schema.

`phoenix.effects.ability_lifecycle` owns activation and paid work policy. Its actor-bound `AbilityLifecycle` mutates the actual component, checks the captured generation after an activation transaction, allocates activation identities, handles recovery and funding pause/resume/stop, advances preparation and publishes work receipts. It re-acquires the component after transactions that can replace or remove grants.

The native port supplies control binding, physical qualification, activation transaction results, request acknowledgement, paid output and apparatus effectiveness. Ordinary World binding units retain command admission/selection and permitted-target capture; they wire those operations into the lifecycle owner. The lifecycle provider passed standalone compilation and its native bindings are activated for the final integration gate.

Read `abilities.cppm` for state and operations, then `lifecycle.cppm` and `lifecycle.cpp` for runtime policy. Serialization retains the existing fields. Relevant production-path system cases include abilities, psychic, rules and process exclusions.
