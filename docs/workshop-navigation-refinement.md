# Workshop lists, mission instances and Navigation

The production base has one reusable ship template, `base.courier`, fitted from the former Rescue player Courier. Test Missions uses the same template. Mission-specific names, policies, crew and equipment differences belong to map instances. Independent development bases retain their existing content.

## Mission instances

An entry in a map's `entities` may include an `overrides` object. It is a JSON merge patch over the inherited entity template: objects merge recursively, arrays replace, and null removes a field. `id` and `extends` are forbidden at the patch root. Normal content validation checks the resulting ship, including its equipment, Controllers, personnel and presentation.

The loader creates a deterministic definition named `<map-id>.instance-<entity-id>` before equipment resolution. A collision is an error. Runtime, Console bundles, dependency collection and continuation use that compiled definition. Workshop retains the authored template reference and patch when saving or reopening; generated identities are not added to the template list.

The instance inspector's **Mission instance overrides** section shows inherited and local fields, and supports reverting individual fields or the whole patch. The template inspector beneath it still edits shared content. Map and whole-map material previews read the compiled instance definitions.

Morrow is a Courier instance in Rescue under threat. Patrol and communications NPC differences are similarly local to their missions. The boarding shuttle preserves its independent fitting rather than inheriting the newly expanded Courier equipment.

## Navigation overlay

Layout schema 1 adds `kind: "overlay"`. Its children have distinct `region` values: exactly one `background`, and optionally `top-left`, `top-right`, `bottom-left`, and `bottom-right`. The background is ordered first and fills the page body. Corner panels use `width`, `height`, minimum sizes and `inset`, scaled with player text. Container padding supplies the outer margin. Existing `scroll`, `when`, themes and component bindings continue to apply.

Corner panels draw above the background and intercept pointer and wheel input within their bounds. Exposed map areas retain normal selection, pan and zoom. Panels are fixed by the author. If the available size cannot accommodate the panels, the explicit compact arrangement is used; without one, the overlay becomes a column with the background first. Opposing corner sizes contribute to the required width/height so panels cannot overlap silently.

Workshop exposes overlays in the palette, hierarchy, properties and live canvas. `region`, `inset` and scrolling survive restricted Console transfer.

Production Navigation uses a map background and one FTL panel at bottom-right. It owns waypoint and FTL commands using Navigation's ordered human preferences. The separate Navigator Console is removed. FTL still uses the shared control widget and ordinary command admission, machinery, readiness and debrief paths. Objectives are not added to a corner. A map-only Navigation presentation supports NPC fittings without FTL.

## Workshop performance and preferences

Document revisions invalidate map verification, UI and equipment previews, labels and themes after edits. Resource revisions track imports and pack/dependency changes. Raw JSON authoring remains supported: ordinary tools detect changes on revision queries; the live editor publishes them at interaction boundaries. Browsing, wheel scrolling and scrollbar dragging reuse caches and do not create undo entries. Undo/redo publish a fresh revision.

Uniform definition and Console lists use ImGui clipping and cached labels. Hidden inspectors, palette panels and canvases skip their work. Variable-height inspectors retain ordinary layout.

Human preferences are numbered rows with Add, Remove, Move up and Move down. The effective home-station-first order is shown before customization; Reset restores the runtime default. The stored format and first-available-human behavior are unchanged.

### Explicit profiling

Set `PHOENIX_WORKSHOP_PROFILE` to a CSV output path and `PHOENIX_WORKSHOP_PROFILE_VIEW` to `entities`, `equipment` or `layout`, then run native with `--workshop-pack <pack> --capture-frames 360 --uncapped`. These optional diagnostic settings select a representative view and scroll long lists without editing content. The CSV separates theme/cache work, lists, map, inspector, layout/preview and history, alongside total frame time. Discard startup/warm-up samples and record hardware and build identity. This is a manually invoked measurement, not a CI timing threshold.

Verification results and graphical/performance measurements are recorded separately in the accompanying smoke receipt. A build or semantic probe does not establish physical reader, touch, gamepad or six-display acceptance.
