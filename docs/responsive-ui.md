# Console layouts and themes

Workshop's **Layout hierarchy**, **Layout palette**, **Layout properties** and **Layout canvas** compose player interfaces. They use the production Console surface with isolated sample observations. Layout editing does not change Station ownership, Controller bindings or information permissions.

## Authoring

Select a Console. New Consoles start with a responsive Operations panel. For an older Console, choose **Create responsive layout** explicitly; saving unrelated changes does not convert it. The conversion supplies a starting arrangement and retains existing widget bindings. Review the result in the canvas before saving.

Choose a panel in the hierarchy or click its canvas outline. Drag hierarchy items or palette components onto a container, or use the Add controls. Rows and columns distribute available space using minimum sizes, preferred sizes, stretch, padding, gaps and alignment. Wrap containers produce rows of panels. Named tab groups supply explicit pages.

Instrument groups retain precise positions within a 1000-by-1000 local coordinate space. Drag an instrument to move it; use its corner handle or Shift-drag to resize it. Numeric rectangle fields provide the same operation. Snapping uses ten local units. Circle-cutout groups offer a circular centre and four rectangular corner regions; the radius is relative to the shorter group dimension. These are supported shapes, not a free-form packing system.

Minimum sizes increase with the player's text scale. When the wide arrangement cannot fit, the renderer uses its compact arrangement or makes the root's named groups into tabs. Instruments that cannot fit reflow into readable columns. Pages scroll when their content needs more room. **Edit compact arrangement** creates an explicit alternative using the same component targets. Verification refuses a compact arrangement that drops or duplicates a target.

The canvas offers desktop, tablet, phone, custom dimensions, rotation and enlarged text. **Interact preview** sends input only to the sample session; Edit mode selects and arranges components. The canvas also selects complexity stars. Sample states include Power and Repair boards with engineering telemetry, conversations, faults, empty conversations and long text. Production binding errors still belong to full-draft Verify; sample Systems are not a substitute for validating the fitted ship.

The existing Properties inspector edits the widget's binding and semantic label. Layout properties edit its placement. Existing specialist panels can be placed whole or split into supported subpanels: operating controls/readouts/messages, engineering cards, sensor controls, conversation directory/header/history/incoming/replies/composer, navigation, manual flight, Power, Repair, Operations, Docking, Flight operations, Personnel and Away missions. Their command handling stays in the existing Console model.

## Complexity and capability visibility

A Console definition, widget, or layout node can have a `when` object. Put it on a tab's child node to hide the whole page, or on a component/group to vary the system UI within a page. Empty containers disappear; when a selected page disappears, selection moves to an available page. Conditions are re-evaluated when the Console's profile or control permissions change.

```json
{"id":"individual-engines","kind":"component","component":"controls",
 "when":{"min_stars":3,"bindings":["controller/port_drive/speed"]}}
```

Supported keys are `min_stars` and `max_stars` (integers 1–3), and nonempty arrays `profiles`, `actions`, `controllers`, `bindings`, `systems`, `features`. Fields combine with AND; identifiers within an array combine with OR. `profiles` names the active authored profile. `bindings` uses `controller/<id>/<channel>`; `systems` names physical consumers of permitted human commands. `actions` names command actions, and `controllers` names Controllers with projected human commands. An unknown star level does not satisfy a star constraint. Verification rejects malformed conditions and unknown keys.

Prefer capability conditions for system controls: two Consoles at the same star level may operate different equipment. Permission checks use the Console's current permitted observation, including individual-channel star thresholds. Temporary readiness, damage or empty ammunition do not by themselves hide an available control. Informational panels can remain visible under AI operation. Controller-bound throttle/heading widgets automatically disappear when their command channel is unavailable; other widgets can opt into `when` conditions.

Specialist components automatically require a matching capability, even if the author supplies no `when` condition. The registry used by the compiler is also the Workshop palette. Features are `power` (owned machinery telemetry), `repair` (repair teams and section observations), `operations` (manual logistics workload with an owned human controller and relevant stores/routes), `personnel` (manual officer assignments and a permitted roster), `away` (missions owned by this Console’s Dispatch), `craft` (craft this Console may order), and `docking` (permitted docking ports). Missing capability removes the component and any empty enclosing page. Downgrading workload or losing permission safely selects an available peer. These conditions never grant simulation authority.

Use **min_stars/max_stars** in Layout properties to add author constraints (zero in the editor means unrestricted). Intrinsic capability requirements still apply. Conditions and `tab` visibility survive Workshop export/reopen and restricted Console asset transfer. Source JSON can express the other supported condition fields.

## Reuse and content

Definitions add three ordinary pack tables: `layouts`, `ui_groups` and `themes`. Each uses UI schema `1`, stable IDs, the normal dependency order, inheritance and explicit override rules. They participate in Workshop history, export/reload and client presentation transfer.

A Console's `layout` is an inline layout or a layout ID. `layout_overrides` is a local merge patch; arrays such as children are replaced as a whole. The hierarchy can reference a template, override it locally, revert the override or detach a resolved copy. **Save layout template** and **Save reusable group** create reusable definitions under the supplied asset ID. Dependency definitions can be copied through the existing explicit override section.

Layout nodes have an `id`, translated source `label` and one of `row`, `column`, `wrap`, `tabs`, `instruments`, `cutout` or `component`. Containers have ordered `children`; component nodes name an existing widget ID or supported panel target in `component`. IDs are scoped beneath their parent. Component targets appear at most once in an arrangement, retaining the original control's binding and semantic identity.

A group instance names `group`; optional `overrides` patch its root. Instance placement and sizing fields apply to that instance. Recursive groups, unresolved references, duplicate scoped identities and unsupported nodes are errors. Trees are bounded to 24 levels and 512 expanded nodes.

`hud_layout` and `hud_layout_overrides` provide the same authoring model for a Console's compact Viewscreen HUD. Its targets are checked against the existing compact widget catalogue. The hierarchy's **Edit Viewscreen HUD** option edits that arrangement separately from the full Console.

The loader compiles layout nodes into typed presentation data and selects responsive structure and page membership. RmlUi flex layout measures ordinary rows, columns and wrapping panels, including their translated text. Custom geometry is limited to precise instruments, aspect ratios and circle-cutout regions. The canvas reads the production surface's measured DOM bounds, also used by rendering and input, rather than resolving another copy of the layout. Unchanged definitions reuse the compiled preview; telemetry and page navigation do not recreate its component tree.

Old packs retain their legacy layout path. New layouts bypass the legacy station-specific placement and outer-frame rules. The ship's physical Controller layout remains a separate concept.

## Console and page tabs

The station bar lists the root pages of each permitted Console at the same level. These pages come from authored layouts, with no globally injected gameplay tabs or runtime rewriting of the layout tree. Power and Repair are live registered components inside those layouts, using the same placement, themes, responsive geometry and semantics as Workshop previews. Common gameplay pages use the same path; Settings contains preferences only.

A Console definition may set `tab:false` (**Show as station tab** in Workshop) for an embedded command source. This hides its auxiliary navigation entry without granting or revoking command ownership; a primary Console remains reachable. Helm declares its target/weapon components directly and omits Operations. Support declares Power and Repair, with Comms on its granted Console and Operations appearing only when its logistics capability is available.

Console switching and layout pages share the tab model. Console tabs come from the current permitted observation: the primary Console and any additional Consoles supplied by authored Station membership or human-seeking grants. The layout editor adds no extra claiming workflow and cannot grant authority.

Switching preserves per-Console page selection, browsing selections, navigation view, conversation draft and panel scrolling. Held input is released before switching or reflow. Revoked tabs disappear and selection follows the remaining authored order. Each routed native surface owns its own presentation state. Tab controls use ordinary pointer, keyboard, gamepad and browser/native semantic paths.

Inactive pages are filtered from the active semantic controls. Reflow that hides the focused control moves focus to its page tab. Observations and notification eligibility continue to use their existing authority and provenance rules.

## Themes

Create or select a theme in Layout properties. The visual fields expose background, panel, text, muted, accent, focus, disabled, error and border colours; font family/size; radius, border width and default spacing. Colour fields also offer colour pickers. **Use theme for pack and menus** selects the manifest's `theme`. A Console may select another theme and apply `theme_overrides`; node `style` supplies explicit component visual properties.

Resolution is built-in defaults, the selected pack theme, the Console theme and local overrides. Player text scaling remains a personal setting. No raw RCSS, HTML or JavaScript is accepted as theme content.

Import PNGs through the ordinary image importer and fonts through **Import font**. Theme `assets` slots include `background`, `panel`, `button`, `font`, `fallback-1` and named instrument artwork such as `helm-radar-ring`. Font bytes remain owned for the RmlUi font engine's lifetime. Instrument artwork uses the existing slot's geometry and calibrated markers.

Panel and button slots optionally use `slices`, four source-pixel insets in top/right/bottom/left order. Verification checks the image and slice dimensions. The RmlUi adapter generates nine-patch decoration; the launcher adapter generates border-image styling. Generated native styles, custom instrument assets and the browser launcher's validated CSS consume the same theme definitions. Unsupported renderer effects are not exposed.

The packager includes the selected public theme and browser-readable artwork in menu metadata. The launcher uses its built-in presentation before selection and applies the selected base/pack theme afterward. Invalid optional theme metadata leaves the built-in presentation usable. Native menus, lobby, player settings, Consoles and HUD controls consume the shared theme; the 3D world is unaffected.

## Verification and visual journeys

`system.content-and-packs` uses synthetic packs through the production loader for layout/group/theme resolution, compact target preservation, tab removal, presentation bundles, history and rename/export/reopen. The existing input/semantics and presentation-transfer checks exercise the surrounding production boundaries. Launcher tests cover safe theme metadata and refusal of executable styles or remote asset URLs.

The standalone native `phoenix_layout_probe` is **manual**, not registered with CTest:

```powershell
./.tools/python/cmake/data/bin/cmake.exe --build build/native --config Release --target phoenix_layout_probe
./build/native/Release/phoenix_layout_probe.exe path/to/disposable/source-pack
```

It edits only its in-memory draft, verifies and reopens an exported archive, and captures desktop, phone and enlarged-text theme previews under `build/`. Use a disposable pack with at least one Console. A successful probe does not establish real screen-reader usability, simultaneous physical input or the six-display performance target.

Asset mission assignments are controls within Personnel and team details, using each open mission's projected slot eligibility. They do not require an Away component on the contributing Console. Only Dispatch gets the Away lifecycle page. Automatic officer workloads omit Personnel; team contributions remain available at lower complexity and keep automatic accompanying-officer selection.

Helm's authored `target-info` component is the selected-contact info panel (`readouts` remains a compatible target). Contact details appear directly in that panel; the system-status action sits beside the radar. Circular contacts retain their sensed footprint through restricted projections and shared radar. Detection and the displayed range use the near surface, and the radar draws and selects the visible circumference even when the centre lies outside the plot. Its single contextual docking control uses the same measured component bounds and semantic/input paths: Dock, Cancel docking or Undock, with status during separation. Helm layouts omit the standalone Docking page. The registered `docking` component remains available for specialist port-permission, supply-service and emergency-release interfaces. Host projections supply eligible owned ports; client presentation does not infer docking permission from compatibility alone.

## Compact station components

A Console may author `header` as an inline schema-1 layout or reusable layout ID, with `header_overrides` using the same merge-patch rules as the body. The default header places `settings` and `console-name` on the left and `tabs` on the right. These three components belong in the header; the measured header reserves the body area. Workshop's **Edit station header** exposes the same component tree, theme, geometry and runtime input paths. Restricted Console assets retain referenced header layouts, reusable groups and overrides.

Task components include `weapons`, `target-info`, `objectives`, `viewscreen-controls`, `alert-controls`, `shield-display`, `ftl-controls`, `compartment-health` and `team-roster`. Components consume permitted observations and retain the originating Console's command admission. A fitting appearing in an informational feed does not confer adjustment authority. Weapon toggles publish accessible pressed state as well as green/red styling and a state icon. Large weapon and team rosters use contextual lists rather than shrinking controls.

The shipped Helm uses weapons above target information on the left, with radar and flight instruments alongside. Dispatch retains Away Missions before reveal, including at one star; automated Personnel remains hidden. Support uses Support, Engineering, Navigation and Comms as peer pages. Engineering puts actual authored resource groups beside compartment health and repair/Medical teams; one-star allocation remains read-only. Individual tuning belongs in the permitted details popup. Comms contains conversation components only. Navigation selection exposes a waypoint action without changing the waypoint; gestures remain available, and a focused map supports arrow-key selection and Page Up/Page Down zoom.

Viewscreen rendering and semantics exclude objective and waypoint presentation, including shared radar/navigation maps. Sharing uses the ordinary admitted display publication and its active toggle; activating the same source again restores the camera. Offensive weapons require a hostile disposition in their permitted contact observation before firing costs are committed. Contact selection and individual target overrides do not bypass that check; defensive interception and scanning retain their own execution paths.

Native startup refreshes an older installed bundled base from the shipped archive without creating a backup mod. Independent Workshop mods remain installed. Saves retain their embedded content and revision contract. VS Code's native launch builds and runs `build/native/Release/native.exe`; station verification must also check the installed library's base hash against the shipped archive.

Support uses the fitted camera-mode and camera-direction widgets rather than a generic camera button bank. Condition is a numbered 1–5 range input mapped to the permitted authored alert commands. Shield status uses the owned ship's side-on sprite, trimmed to its visible pixels, with semicircular pool arcs and adjacent percentage readouts. The same readouts remain available to assistive technology; manual shield controls appear only when permitted. FTL presents Prepare/Cancel before Depart; Depart is visibly and semantically disabled until the authoritative readiness projection permits departure.

Comms uses compact message spacing without reducing text size. Optional incoming, reply and composition components use selected-conversation availability features, so empty or unavailable components do not reserve body space. These conditions use the ordinary Workshop/runtime layout compiler and survive content export and transfer. Participant status shares a line, and the composition field aligns with Send.
