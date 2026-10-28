# State trees

Start with `state_tree.cppm` (`phoenix.state_tree`). `state_tree_definitions.cpp` resolves inheritance, validates typed registries and compiles programs. `state_tree.cpp` evaluates programs and owns continuation encoding, interruption and resumption. `state_tree_validation.hpp` is private to this module.

Link `phoenix_behavior_state_tree_definitions` for compilation or `phoenix_behavior_state_tree` for execution. Both depend only on portable foundation contracts. Callers supply vocabulary, permitted observations and output execution; the tree cannot inspect World state or mutate equipment itself.

Program definitions are immutable. Each `Instance` owns private memory, active paths and activation identity. Preserve task/output ordering and save/resume behaviour. Existing synthetic gameplay, Controller, conversation and scenario cases exercise the interpreter through production adapters; no test per node or implementation function is required.
