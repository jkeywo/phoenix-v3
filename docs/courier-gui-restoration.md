# Courier GUI restoration

Agreed with the user on 30 October 2026. Implemented and verified on 30 October 2026. Cruiser is out of scope.

Helm retains radar in the centre, throttle/status on the right, and compact weapons/missiles/selected-target panels on the left. Flight mode and On screen occupy upper radar corners; lateral buttons occupy the lower gaps. Narrow screens retain columns, compact weapons first, then scroll the left column if necessary. Helm/Away Missions are the two lower-star tabs; Personnel appears at three stars.

Cannon/beam tiles show enablement, ammunition/charge and condition. PDC shows ammunition and battery even when automatic at one star; two stars adds enablement; three adds Hold fire/Intercept/Selected target. Three-star weapon settings expand inline. Target copies the radar selection; there is no clear-override control. Existing downgrade behaviour remains.

Missiles are grouped. Fire launches eligible loaded tubes once, reports blocked tubes, and never queues later launches. One type is chosen for the group: automatic at one star; named types only from two stars. Type selection, loaded/reserve counts for the current type, readiness and Fire are on the panel. Actual loaded ammunition remains authoritative during changes.

Courier gains existing lateral-flight capability, initially half main-drive acceleration and speed. Manual lateral control starts at two stars and works in assisted and manual modes. This is initial tuning, not a fixed performance assertion.

Away Missions starts with a list. Select opens details; Activate starts preparation. The next row contains officer portraits/names and a delivery-method portrait. Launch is deliberate and uses existing minimum staffing/delivery requirements; optional slots do not block. Three-star roster/delivery tiles open eligible choices.

Support has full-workspace Overview, Power, Repair and Comms views. Overview retains objectives/compact FTL, viewscreen/alert controls, shields and Veil. Power shows reactor/shared reserves and group rows below three stars, with individual Systems grouped beneath their group at three. Two stars adds reactor output. Shared reserves are deduplicated.

Repair includes Repair and Medical. A narrow roster is left; the ship map is right. Select a team, then click a compartment to send it. The map shows compartments and team location/destination. Below it, details concern only the team's physical current compartment and are unavailable during travel. Three-star orders use that selected-team detail area. Preserve existing knowledge and work-permission boundaries.

Personnel at three stars has posts/portraits/details on the left; mission slots and spare officers on the right. Click a slot to select an eligible officer. Filled mission slots stay visible. Preserve department scope, reservations, qualifications and physical travel.

Implementation reuses specialised renderers/input paths with explicit widget-part bindings. Intentionally unexposed commands remain separate from authority. Courier verification refuses generic presentation fallbacks. Request identities, physical weapon/resource checks and ordinary mission admission remain authoritative. No unrelated or Cruiser content changes.

Verification must exercise real GUI workflows at every star and normal/narrow/text-scaled layouts, inspect captures, verify content, run native/WASM integrations and continuation checks. Commit verified stages; record actual evidence here before declaring completion.

## Implementation and verification

Courier has dedicated Console/widget definitions so the restoration does not alter Cruiser's authoring. Only the existing `base.courier` entity changes; original maps, trees, other entities and pre-existing shared widget/Console definitions remain unchanged. The lateral buttons bind separate port/starboard parts of the same direct-flight Controller.

Specialised weapon presentation uses the shared resolved widget data model for Main/Details/Hidden. Details reuse the existing widget popup. Loaded missile identity comes from the physical loaded magazine/resource, independently of selected reload type. Native Fire uses the existing request identity, authority and receipt machinery; its physical preparation consumes the request once, including blocked tubes. Synthetic coverage verifies partial fire, replay, no later launch after reload/re-enablement, and deterministic save/restore.

The personnel picker gets eligible duties from a read-only call to existing assignment admission, including department, reservation, qualifications and physical route checks. Mission launch readiness and dispatch share the same readiness check. New permitted readiness/portrait fields survive remote projection without a wire/save schema bump.

| Check                               | Evidence                                                                                                                                                                                                                             |
| ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Native application and build check  | Isolated `build/widget-finish-native`; `courier-native-build-check.log`                                                                                                                                                              |
| Browser client, Workshop and server | Isolated `build/widget-finish-web`; `courier-web-final-build.log`                                                                                                                                                                    |
| Native integrations                 | 5/5: content/packs, Controller runtime, combat, away, personnel; `courier-native-tests.log`                                                                                                                                          |
| WASM integrations                   | 4/4: Controller runtime, combat, away, personnel; `courier-wasm-*.log`                                                                                                                                                               |
| Narrow-layout regression            | Explicit scrolling instruments retain their structure; native and WASM Controller tests rerun                                                                                                                                        |
| Continuation                        | Fresh Controller, combat and away native/WASM traces match; `courier-*-native.trace` / `courier-*-wasm.trace`                                                                                                                        |
| Content                             | `courier-content-verify.log`; base pack verifies with all specialised and split-part bindings                                                                                                                                        |
| Shared widget regression            | Existing synthetic widget/Workshop journey passes; `courier-generic-widget-smoke.log`                                                                                                                                                |
| Courier graphical journey           | Helm and Support at all stars; normal/narrow/130% text, throttle, Fire envelope, contact navigation, manual/assisted lateral input, selected ammo versus physical load, mission activation/roster, and map assignment/travel privacy |

Graphical captures and logs are under `build/courier-helm-final*` and `build/courier-support-final*`. They are manually triggered evidence. `tests/manual/courier_ui_fixture.py` creates a disposable scenario from the verified real Courier pack: it reveals Rescue through its introductory conversation and brings its contact into local radar range. It changes no ship, staffing, authority or physical admission rules and does not replace the independent synthetic integration fixtures.

To reproduce the graphical journey after building its explicit target:

```powershell
python tests/manual/courier_ui_fixture.py build/widget-finish-native/base.zip build/courier-ui-fixture.zip
build/widget-finish-native/Release/phoenix_control_widgets_smoke.exe --courier build/courier-ui-fixture.zip smoke.courier-ui courier base.courier helm build/courier-helm-final
build/widget-finish-native/Release/phoenix_control_widgets_smoke.exe --courier build/courier-ui-fixture.zip smoke.courier-ui courier base.courier support build/courier-support-final
```

Changed-file C++/CMake/JSON/Markdown formatting checks pass. The repository-wide formatter still reports pre-existing drift in untouched `src/content/documents/types.cppm`. Physical screen readers/devices, a six-display installation and browser graphical acceptance are separate evidence; these checks do not claim them.
