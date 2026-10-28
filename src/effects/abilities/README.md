# Ability execution state

`phoenix.effects.abilities` owns ability grant continuation, recovery keys and reservation accounting. Physical adapters supply immutable ability definitions and the fixed simulation tick. Source qualification, native target checks and physical execution remain with the appropriate World adapter.

Read `abilities.cppm` for state and operations, then `abilities.cpp`. Serialization retains the existing fields. Relevant production-path system cases are abilities, psychic, rules and process exclusions.
