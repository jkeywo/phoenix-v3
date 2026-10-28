# Scenario situations

Start with `scenario.cppm` (`phoenix.scenario`). `scenario_definitions.cpp` compiles actors, spawn groups, objectives, conditions and report declarations. Region compilation and geometry are delegated to `scenarios/environment`. `scenario.cpp` owns situation/action identity, objective progression, pending publications and continuation. Its headers are private to this module.

`phoenix_scenarios_runtime_definitions` links foundation, state-tree definitions and environment geometry. `phoenix_scenarios_runtime` adds scenario execution. It receives actual committed positions/motion and exposes supported requests; World construction, stock/effect changes and permitted knowledge publication remain adapter responsibilities.

Preserve action deduplication, population bounds, event ordering, authoritative objective transitions and save/restore fingerprints. Existing scenario/mission and resumed-operation cases exercise real content loading and execution without production-content goldens.
