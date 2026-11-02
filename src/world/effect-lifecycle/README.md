# Effect lifecycle integration

Start with [effects_rule_lifecycle.cpp](effects_rule_lifecycle.cpp). Authored rule sources, clock/lease qualification, upkeep quotes/commit, region triggers and continuation binding. RuleApplications owns canonical evaluation.

This is private native integration in `phoenix.simulation:effect_lifecycle`, not a public domain library. Each ordinary unit imports this folder's partition, which reaches the canonical Runtime. [World core and phase order](../README.md) own lifetime, scheduling and candidate replacement; the independently compiled domain owners own mechanisms/state below that composition.

`sources.cmake` registers this workflow explicitly into the simulation target through the World family. File placement changes no tick order, schema or authority. Cross-domain policy remains visible here; there is no exported universal World context.

`Runtime::RuleSourceReadScope` shares physical source eligibility scratch only across a read-only observation batch. Nested readers reuse the enclosing scratch; destruction restores the previous pointer before mutable execution. Live rule/output/expiry checks remain outside that cached eligibility. Immutable catalogue trigger metadata may reject events with no possible subscriber before enumerating sources. Neither optimisation supplies knowledge to a Console or Controller.
