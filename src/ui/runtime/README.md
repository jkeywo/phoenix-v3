# Runtime compositor and Rml integration

Start with [presentation.cppm](presentation.cppm).

screen.cpp composes screens/surfaces and routes platform input. rml.cppm owns the renderer, font engine and memory-image registry with explicit init/shutdown. Renderer geometry and clipping state are private to rml.cpp. phoenix.presentation exposes only runtime composition and development probes; consumers import graphics, Rml, preview and accessibility interfaces directly. pad_dispatch.cppm owns gamepad navigation and repeat state. accessibility.cpp translates Application state into portable platform snapshots and applies returned semantic actions.

Dependencies: Application/session APIs, graphics, player UI modules and platform adapters. The viewport adapter owns native/browser focus and browser surface-size synchronization. No authoritative simulation.

Validation: Native/browser role builds and existing semantic/input cases; actual device/reader/performance acceptance is separate.
