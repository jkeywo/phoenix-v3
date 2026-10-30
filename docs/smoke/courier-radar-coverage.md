# Courier radar coverage — 30 October 2026

## Delivered behavior

The production Courier has two full-traverse gun turrets, one beam laser, two independently controlled torpedo tubes sharing the existing magazines, and its retained PDC. The radar groups equivalent coverage, names the members and count, and removes mount/aim duplicates. Homing selections hide torpedo coverage, Havok selections show it, and PDC coverage follows enablement/targeting policy. Directional sensor sectors are hidden on Courier Helm through console data.

See [the authoring contract](../radar-coverage.md). Existing saved fittings remain embedded in their saves; start a new session for the revised fit.

## Executed checks

- Native Release builds: `native`, `phoenix_combat_tests`, `phoenix_content_tests`, `phoenix_control_widgets_smoke`, and the base/test-missions packs. Both packs verify through the production packager.
- Native combat suite: passed, including the new coverage contracts. The focused `phoenix_combat_tests.exe --radar-coverage` invocation also passed. Synthetic cases cover independent shared-magazine loading, selected-ammunition visibility during loading, PDC enablement, console feed restrictions, grouping/member names, full-traverse firing at rotated headings, continuation and invalid authoring.
- Native content suite: passed pack round trips, inheritance, ordered overrides and runnable-content verification.
- Both `architecture.*` CTest cases: passed. Native `--build-check`: passed.
- Threaded WebAssembly builds: `client`, `server`, `workshop`, `phoenix_combat_tests` and `phoenix_content_check` passed. From `build/web`, `node phoenix_combat_tests.js` and `node phoenix_content_check.js` passed. Emscripten emits its existing pthread/memory-growth performance warning; this receipt makes no browser performance claim.

Build/test logs are under `build/courier-*.log`. Builds requiring compiler temporary-file access were rerun outside the restricted sandbox. An earlier combat fixture incorrectly started its defensive policy in Hold Fire; selecting interception explicitly fixed that fixture before both final combat suite passes.

## Explicit graphics probe

```powershell
./build/native/Release/phoenix_control_widgets_smoke.exe --radar-coverage build/native/base.zip base.rescue-under-threat courier base.courier helm build/courier-radar
```

The Windows OpenGL probe passed against the production fitting. It checks all three complexity levels, the six fitted weapons including PDC, absence of duplicate mount outlines, full gun coverage, grouped accessible gun descriptions, mixed homing/Havok selections, PDC on/off, and actual ship headings of 90 and 180 degrees reached through ordinary Controller commands.

Captures were visually inspected for readable labels and non-overlapping coverage. The first capture revealed legend text obscured by the instrument bezel. The final run places a larger legend inside the plot on dark backing and passed after that correction.

Final captures:

- `build/courier-radar-stars-1.png`, `-stars-2.png`, `-stars-3.png`.
- `build/courier-radar-mixed-pdc-off.png` and `-mixed-pdc-on.png`.
- `build/courier-radar-heading-90.png` and `-heading-180.png`.
- Log: `build/courier-radar-smoke-final.log`.

These are manually invoked graphics and semantic-model observations. Actual screen-reader operation, physical controls and six-display performance were not exercised.

## Formatting and shared workspace

Task changes pass targeted formatting and diff-whitespace checks. The full formatting check reports drift in the untouched document-type module and concurrent artwork work, including the other task's starbase changes inside the shared definitions file. Those unrelated changes are preserved and excluded from the Courier commits.
