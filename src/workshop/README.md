# Workshop and GM

Read `shell/shell.cppm` for shared controls and the render-frame contract, then
`workflow/workflow.cppm` for the authoring workspace and `gm/gm.cppm` for live
inspection. Authoring owns editable documents; inspection owns only permitted
session views. They share the ImGui backend and graphics without sharing draft,
selection, undo or map-camera state.

The dependency direction is shell -> asset previews/map navigation -> map,
layout, tree and test client -> gameplay inspectors -> document workflow. The
top-level `phoenix.tools` module composes these with Application routing; editor
interfaces themselves do not accept Application. Browser testing sends bounded
requests to a separately linked authoritative host.

Every leaf registers its own CMake target. No tool target may transitively link
World, Flecs or authoritative host/session implementation. GPU resources and
input capture must be released before the graphics/backend context is destroyed.

Existing content, menu, input/semantics and disposable Workshop-host system cases
protect shared contracts. Graphical layout/controller/GM and localisation
journeys remain explicitly invoked smoke evidence.
