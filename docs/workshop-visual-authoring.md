# Workshop visual authoring

The Workshop uses its existing document, production content compiler, ImGui docking and isolated Console renderer. Map selections, graph view state and layout selection are editor state, not gameplay content.

## Map & Scenario

Map and Scenario share one centre panel and map selector. Map settings, presentation assets and gesture help live in a collapsed section. Related field/action controls reserve space for their buttons and wrap when needed.

Map artwork uses authored render height in metres, converted to map millimetres and multiplied by zoom. Directional sheets and legacy image sets follow entity heading. Small selection markers remain usable when artwork is distant.

Click selects one instance; Shift-click toggles membership. Drag empty space for a selection rectangle; Shift-drag adds. Drag a selected entity to move the selection, preserving the initial grab offset and relative positions. Middle-drag pans and the wheel zooms. Escape or losing application focus cancels a move. Touch defaults to pan/pinch; **Touch select/move** enables single-contact editing, with two contacts reserved for navigation.

The Entities list shares the map selection; its per-row checkboxes provide an additive selection route without holding a modifier. Duplicate copies the selected instances, including their instance overrides, and selects the copies. Delete acts on the selection. Selection is cleared on map changes and undo/redo.

For several selected instances, Properties shows common compatible scalar fields and **Mixed** values. Editing applies only the changed field to the selected mission instances. It never changes their reusable templates. Position is edited using group X/Y offsets; heading sets each instance's heading without rotating the formation. Individual override sections or all overrides can be reverted together. Bulk changes validate a candidate document before replacing live content.

## Ship setup

Double-click a definition or choose **Open ship setup**. The centre canvas shows fitted Systems, assemblies, compartments, Stations, Console seats, Controller instances and resource records. Filter categories, zoom and pan the graph; the node list provides a keyboard-accessible alternative.

Select an item to edit its Properties. Drag a source onto a destination and choose a compatible binding field. Connections map to the existing authored fields and are checked through production verification. Click a labelled connection to inspect or remove its binding. **Add ship item** creates editable draft items; finish their required fields before saving or testing.

Inherited items require an explicit local override. Assembly-owned Systems edit their placement overrides; expanded System identities must not be copied into the ship's explicit fitting. Resource records from an assembly lead to their owning placement.

## State trees

The left State trees database lists reusable definitions, dependency trees and local owner bindings. Double-click an entry to open its centre canvas. Tree bindings also offer New local tree, Copy referenced tree locally and Open tree editor actions.

The canvas arranges states in hierarchy order without changing authored execution order. Double-click a state to collapse or expand its descendants. The selected state's transitions show their direction and reason. Properties edits tasks, conditions, bindings and state details. Drag a state onto another state to reparent it. Reparenting refuses cycles and maintains direct initial-child references. Renames repair internal references; deletion requires resolving references first.

Embedded trees remain embedded when saved. At load time, tree-bearing fields become ordinary private catalogue references with deterministic identities based on owner IDs and field paths, independently of tree contents. Existing inline widget policy encodings are retained. Compilation errors identify the local owner. Reusable and local policies use the same runtime compilation, dependency and continuation paths.

## Console GUI building

Select a Console to open its existing palette, hierarchy, canvas and Properties. Drag components, Controller widget parts, reusable groups or layout containers directly onto the canvas. Target outlines and insertion lines show where a drop will land. Dropping a container wraps an existing element, so the new layout stays structurally usable.

Responsive rows, columns and pages use order and containment. Instrument panels support free movement, visible resize handles and snapping. Hierarchy drag/drop and move buttons provide alternate editing routes. Invalid moves are refused without removing the original element. Escape or focus loss cancels instrument movement/resizing.

Primary and compact arrangements are edited separately. The preview renders the selected arrangement in isolation; ordinary pack verification still requires both arrangements to expose equivalent logical controls. Edit mode manipulates layout; Interact preview exercises isolated sample controls. Desktop/tablet/phone sizes, sample states, complexity and text-scale previews remain available.

## Verification

Synthetic content and Workshop-host cases cover bulk edits, rollback, group translation, stable owner targets, local-tree roundtrips, reparenting and real Controller continuation. These fixtures do not inherit production ships or tuning.

The manual map authoring probe loads the independent three-instance fixture and drives production pointer samples through the real editor, checking rectangle selection, relative group translation and cancelled movement. It is explicitly invoked and does not claim physical touchscreen evidence.

The manual layout probe accepts `--interactive` after its disposable pack argument for direct graphical inspection. The Workshop runtime test can emit its independent authoring fixture with `--write-fixture <path>`. These modes are explicitly invoked; they are not automatic graphical acceptance tests.

Build and smoke results are recorded separately in the accompanying receipt. A build does not prove touch hardware, keyboard usability, localisation layout or native/browser graphical acceptance.
