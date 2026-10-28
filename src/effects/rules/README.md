# Rule evaluation

`phoenix.effects.rules` evaluates compiled typed expressions and prepares immutable effect quotes from frozen facts. It has no World, Flecs, renderer, platform or authoritative source-discovery dependency. Catalogue compilation is owned by `content/effects` and native fact gathering remains an adapter responsibility.

Read `rules.cppm` then `rules.cpp`. Evaluation preserves checked arithmetic, missing-fact failure and the existing quote semantics. Relevant production-path system cases are rules, processes, abilities and custom properties.

Authoritative rule clock/source integration is being extracted separately from the evaluator; the remaining integration policy lives in ordinary `src/world/bindings/effects_rule_*.cpp` units and is not automatically an independent rule module.
