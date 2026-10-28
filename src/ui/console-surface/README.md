# Console surface host

`ConsoleSurface` owns one Rml context/document, render cache, observation scope, authored placement and event routing. Begin with `surface.cppm`, then `surface_state.hpp`; `layout.cpp` composes a frame, `interaction.cpp` routes input and `drawing.cpp` presents it.

[Document services and choice descriptions](../surface-host/README.md) own generic DOM caching/placement and data-defined choice controls. [Conversation panels](../information-widgets/README.md) and [navigation/mission panels](../navigation-panel/README.md) own their complete domain state and operations. Existing flight/tactical/engineering/operations widget owners retain their specialized mechanisms. Engineering/support/manual composition and authored layout remain in this host; these are explicit remaining coupling, not hidden behind a universal panel callback interface.

The host builds bounded panel frames from Application metadata and permitted ConsoleModel observations. Panels retain neither those frames nor Application pointers. It owns context/document lifetime; all panels/caches are destroyed with the surface before Rml shutdown. Probe methods remain manually invoked diagnostics.

See [the refinement record](../../../docs/understandability-refactor.md) for code growth and executed native/browser/system/graphical checks. Hardware readers/devices and multi-display performance retain separate acceptance.

The built-in Rml shell and styles live in `content/ui/console/surface.rml`. They are presentation data copied into native/browser assets; the constructor loads the same bytes before its existing skin substitution. The template is an explicit link dependency so edits refresh WASM preload data and native asset copies.
