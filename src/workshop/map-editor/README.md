# Editable maps and shared navigation

`phoenix.workshop.map` owns the authoring map camera, pointer gestures, resolved
catalogue cache and graphics assets. `map.cppm` takes explicit document,
MapEditorSelection, feedback, saving state, tool-frame and save callback inputs.
The workflow owns selection and document/history; the map owns presentation state.

`phoenix.workshop.map_navigation` contains the shared gesture and icon geometry
used by editable maps and GM inspection. It has no document or Application state.
GPU asset lookup is delegated to WorkshopAssetPreview. Clear caches when the
document changes or the workspace closes; preserve existing authored positions.

Existing content and input/semantics cases cover shared contracts. Actual map
selection, dragging and simultaneous touch are explicitly invoked smoke checks.
