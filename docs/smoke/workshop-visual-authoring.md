# Workshop visual authoring evidence — 30 October 2026

This receipt distinguishes production-path checks, explicitly invoked graphical
probes and outstanding device acceptance. It does not claim that compiling a
canvas verifies its interactions.

## Synthetic production-path checks

`phoenix_content_tests` passes with the new Workshop cases: group translation,
atomic rejection of invalid bulk changes, instance/template isolation, override
reversion, undo/redo, cancelled-move history, stable array-row targets, embedded
scenario-tree compilation and source roundtrip, stable owner identities and
hierarchical reparenting/cycle rejection. Reopened policies can be edited again;
identical policies on separate owners get separate private identities, and
reordering owners preserves their identities.
Derived ships also retain their authored `extends` source after export/reopen,
while the production fitting preview resolves inherited Systems for the graph.

`phoenix_workshop_test_tests` passes. Its added Controller policy is embedded in
the widget definition, exported and reopened through Workshop, loaded into the
production World and compared after uninterrupted versus restored continuation.
The disposable Workshop host also executes that embedded policy.

Neither suite depends on shipped ships or balance values.

The native content, Workshop-host, layout-probe, map-probe and packager targets
build successfully. The browser `workshop` target also links successfully. The
production base and Test Missions both pass source verification, archive
verification and client-companion generation through the rebuilt packager;
their tuning is not asserted by the synthetic checks.

The WASM content check passes under the bundled Node runtime, launched from
`build/web` with `phoenix_content_check.js` so its preloaded fixtures resolve.
This is a loader/compiler check, not browser rendering evidence.

## Explicit native graphical probes

`phoenix_map_authoring_probe build/workshop-smoke.zip` passes. The fixture is
emitted by `phoenix_workshop_test_tests --write-fixture
build/workshop-smoke.zip`. The probe opens a real native tool window and feeds
production touch-pointer samples through `MapEditor`: rectangle selection of
three instances, offset-preserving group movement and cancellation restoring
all starting positions. The output is `build/workshop-map-selection.png`.
These samples establish editor gesture handling, not physical touch hardware.

`phoenix_layout_probe build/workshop-layout-smoke.zip` passes its existing
21-stage graphical journey: disposable layout edits, export/reopen, phone tabs,
theme and text-scale previews, wrapping, cutout geometry and capability-filtered
pages. This uses an independent unbound Console fixture. Running that older
journey against the Controller-bound fixture was refused because its legacy
layout conversion discarded a required Controller part; that refusal was not
counted as a successful journey.

Native interactive inspection confirmed the shorter Map & Scenario header,
inline Console identity/rename controls, selection through the Entities list and
artwork picking synchronised with Properties. Fast automation-generated mouse
drags were not reliable with the polling input backend; the production-pointer
probe above supplies the map gesture evidence.

## Acceptance boundary

The final additional Computer Use session was stopped by the physical Escape
key. Further native/browser graphical interaction was therefore stopped. The
browser downloaded the built runtime, but startup/rendering was not confirmed;
this is not recorded as a browser graphical pass.

Physical touchscreen pan/pinch and Select/move, full keyboard journeys, narrow
panels, increased global UI scale and pseudo-localisation still need explicit
device journeys. The existing layout probe does not establish the new palette
drop, graph-port drag or tree-reparent gestures. Browser rendering and interaction
evidence is recorded only when exercised separately from WASM compilation and
Node-hosted checks.

Local fixture archives, logs and screenshots are disposable files in `build/`;
they are not production content or committed tuning snapshots.
