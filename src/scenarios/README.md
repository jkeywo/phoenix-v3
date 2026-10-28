# Scenarios

Read `environment/` first for authored regions, exact geometry and deterministic fields; `runtime/` for situation/action/objective execution; then World-facing integration and objective/debrief composition in their module folders.

Environment operations are pure functions over compiled definitions and have no state-tree, World or renderer dependency. Situation execution owns progression and continuation but requests physical work through supported adapters. World composition controls phase placement and the committed observations supplied to each situation.

Preserve actual motion geometry, supported action identity, knowledge/publication scope and departure-report timing. Scenario presence does not give an arbitrary author access to private Controller memory or unrestricted World mutation.
