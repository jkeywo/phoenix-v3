# Psychic apparatus

`state.cppm` owns apparatus contributions, ghost continuation and reception
records. Scenario interdiction continuation belongs to `scenarios/interdictions`. Fitted apparatus and the
authoritative World own their instances; ability execution remains the activation
authority. Derived funded work is intentionally omitted from saved contributions.

`serialization.cpp` preserves the existing checkpoint fields/default rules.
The module depends on fixed-point World state and state-tree continuation types.
`projection.cppm` owns fixed-step ghost shape physics and apparent bodies.
`receptions.cppm` owns reception identity, fading, rejection and retirement
receipts. `contributions.cppm` reconciles committed ability activations and
computes their funded work; its target port supplies only the requested pose.
`interference.cppm` selects wards and scramblers from ordered source facts,
preserving ward precedence and equal-strength ties. `emission.cppm` measures
native apparatus, recent weapons and sent messages.

World currently binds those operations to fitted apparatus and permitted sensor
feeds through ordinary `src/world/sensor-bindings/gameplay_psychic.cpp` and
`src/world/scenario-bindings/gameplay_interdictions.cpp` units. Native board, save/restore and encounter
integration retain explicit native policy. `PsychicInterferenceMemory` owns
the derived ordered-source cache; `PsychicEmissionMemory` owns recent weapon
and message memory, its 20-tick retention and continuation. The final native
and ordinary/threaded WASM project gates passed, including psychic and
interdiction continuation checks; see the
[verification record](../../../docs/modularisation-evidence.md).

Emission-memory restore installs records incrementally into a fresh candidate. Public World restore validates the complete candidate before swapping it into the live session; the low-level memory restore alone does not provide an in-place rollback guarantee.
