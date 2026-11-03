# Console and Controller definitions

Read compiler.cppm and types.cppm first.

Console ownership/profile declarations, command sections, actions, candidates and bridge settings.

Dependencies: Foundation commands, generic state trees, knowledge and explicit catalogue compiler collaborators.

Ownership: ControllerTreeBindings is compiled setup data; it is not private instance memory or saved continuation.

WidgetReadoutBinding also belongs to the definition contracts. The widget compiler validates source fields against portable presentation encoders and telemetry IDs, and compiles permitted-board pointers. Prepared Catalog values retain these accessors in widget_readouts; authored JSON and exported packs retain their original bindings. Optional permitted values may remain unavailable without invalidating a supported binding.

Validation: Controller runtime, handover, content and knowledge integration cases. These are existing integration surfaces; this guide does not claim they have run for a particular revision.
