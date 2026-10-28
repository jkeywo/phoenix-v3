# Scenario-to-gameplay bridge

`gameplay_scenario.cppm` exports `phoenix.gameplay_scenario`; link `phoenix_scenario_bridge`. GameplayScenarioBridge binds compiled scenario/tree definitions to supported native actions, scoped knowledge readers, effect requests and stable receipts. `gameplay_scenario.cpp` implements this adapter and its continuation.

The bridge depends on content compiler/definition contracts plus generic scenario and tree execution. It does not own a physical World or create arbitrary entities itself. World supplies committed body/knowledge observations and fulfills admitted requests at the existing phases. Scenario selectors must not become an unrestricted private-state lookup.

Use existing scenario-situations and scenario/World cases to verify action identities, event cursors, required reader fields and restored receipts. Current migration/platform runs are in the evidence; browser execution is a separate gate from compiling this bridge.
