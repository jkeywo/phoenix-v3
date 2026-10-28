# Behaviour programs

Read `state-tree/state_tree.cppm` for the generic typed program, compiler and private-instance execution contract. Read `vocabulary/gameplay_vocabulary.cppm` for Phoenix's stable observation/action indices and context registries.

The evaluator receives observations and returns supported outputs. It owns neither a World, a Console command section nor physical work. Controller, scenario, conversation and team adapters supply their own permitted vocabulary and enforce the consequences of those outputs.

Compilation/validation and execution have separate targets. UI and authoring consumers can compile definitions without linking the evaluator or authoritative simulation. Runtime instances retain their own memory, call activation IDs and interruption/resume state.
