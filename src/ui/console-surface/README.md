# Independent per-Console surface

Start with [surface.cppm](surface.cppm).

ConsoleSurface owns one private Rml context/document, render cache, capture/focus and authored placement. Domain widget objects own their own model/feedback state. Destroy surfaces before Rml shutdown.

Dependencies: Console model, Application composition, Rml backend, graphics and the five widget modules. Private surface_state.hpp is used only by this module.

Validation: Native/browser compilation and existing Console/semantic checks. Physical readers, devices and display performance remain manual evidence.
