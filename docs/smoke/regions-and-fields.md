# Region environment and radar smoke evidence

Date: 20 October 2026. These are explicitly invoked graphical checks, separate from automated integration and physical bridge acceptance.

## Automated checks

- Native application build and both content packs verified successfully. All 21 native integration cases pass (`build/environment-native-delivery-tests.log`).
- Server, client and Workshop WebAssembly builds pass. All 18 Node/WASM integration cases pass, including the new region, tag, field lifecycle and authoring cases.
- Native/WASM continuation traces match. The independently generated field-state fingerprint is `18140329632779166180` on both platforms; this is compared within this run, not a committed expected-content golden.
- Production-loader cases cover annulus holes and swept crossings, rectangle path clipping, whole-segment sensor attenuation, overlapping optical depth, zero settings, directional reach, empty/mismatched radar tags, per-radar contact identities, public marker projection, stable entries/spin, overlap composition, separated ships, unload/revisit and recovery.
- Workshop document checks round-trip named entries, tags and displayed nebula regions through verified export/reload, Console asset packaging and undo/redo.

## GPU smoke

The final `phoenix_comic_probe` passes on native OpenGL and actual in-app-browser WebGL 2. It verifies entry selection with atlas half-boundaries, rotated silhouettes, unchanged instance upload counters during spin/fog changes, batch updates on spawning, intervening fog, clipped radar reach, exactly one sample write per frame, no repeated-timestamp update, and wrapped circular-buffer indices after 2,056 frames. Existing sprite depth, normals/lighting, shadow and nested-target cases also pass. Native captures and logs are under `build/environment-smoke/`; browser verification used the served `phoenix_comic_probe.html` and its visible output/console logs.

The first browser sweep check caught `sample` being reserved in GLSL ES despite compiling on desktop GLSL. Renaming the local shader variable fixed the failure; the final browser run reports the radar sweep PASS alongside the other rendering cases.

The actual native Viewscreen loaded `training.nebula-belt` from the verified Training pack using an isolated local profile. `viewscreen-smoke.png` shows the bounded violet volume, obscured background rocks, clear rocks outside the volume and untouched interface frame. This is a simple bounded volume, not a turbulent particle-cloud effect.

A separate uncapped single-window run of that map collected 600 samples after warmup, at 1280Ã—720: frame p95 **4.85 ms**, maximum **6.17 ms**. This measures CPU wall/presentation time, not isolated GPU execution. The output and framebuffer are `build/environment-smoke/training-performance.json` and `.json.png`. It is not a six-display performance claim.

A later native Navigation Console capture could not run because GLFW/WGL failed to initialise a graphics context (`navigation-markers.log`). Its marker projection and authoring are covered by integration checks, but that attempted capture is not visual evidence. Full Workshop control interaction, visual streaming transitions, physical readers and six-display acceptance remain manual follow-up checks; no headless result is presented as proof of those observations.

## Review-fix verification

The follow-up native gate passes all 21 integration cases plus all eight signalling/network service cases (`build/fixes-native-gate.log`). The full web gate passes all 18 WASM integration cases and the exact same-run native/WASM continuation comparison (`build/fixes-web-gate.log`). Native and web application targets build; both content packs verify. The field fingerprint remains equal on both platforms within this run.

Additional synthetic cases exercise collider extents, actual same-tick propulsion into previously unloaded rocks, collision-driven moving-rock unload/save/revisit, paid radar power limits and stale evidence/reacquisition. Narrow circle, rectangle and annulus sensor fixtures check all 360,000 integer bearings against the production visibility path. Projected adaptive profiles round-trip through network and shared Viewscreen allowlists without revealing hidden region geometry. Invalid profiles are rejected.

The explicitly invoked OpenGL and actual in-app-browser WebGL 2 probes both pass the follow-up GPU checks. Counters confirm that changing one asteroid uploads one record without reallocating, crossing a chunk boundary uploads at most the removed/swapped and appended records, and camera/time changes upload no instance records. A distant 100-kilometre batch is culled and leaves local shadow coverage unchanged. A wide, pivoted and depth-offset ordinary sprite retains calibrated shadow bounds. The cached radar boundary uploads once across unchanged frames; a deliberately narrow blocked interval clips fragments even between procedural mesh samples. Native evidence is in `build/environment-smoke/fixes-gpu.log`, `field-distant-batch-smoke.png`, `calibrated-shadow-bounds-smoke.png` and `radar-narrow-blocker-smoke.png`.

Rotating tangent-normal colour diagnostics pass at zero and 90 degrees without instance uploads. The paired native `field-shadow-initial-smoke.png` and `field-shadow-rotated-smoke.png` captures were inspected: the synthetic relief shadow rotates with the sprite. The deliberately discontinuous low-resolution test depth produces sharp wedges; these captures are alignment evidence, not a claim of smooth or watertight geometry.

A self-contained disposable pack placed an inherited named-entry asteroid with the `turned` placement override into the actual native Workshop map canvas. `build/environment-smoke/workshop-smoke.png` shows the rendered thumbnail and selected override; `workshop-entry-preview.log` records successful verified loading. Production document cases separately verify physical/decorative field type reference renames, undo/redo and export/reload. This canvas check does not establish every interactive Workshop editing journey.

The adaptive radar profile targets one-metre uncertainty with a 1,024-interval cap. Sharp boundaries can exhaust that cap; the result remains conservatively shortened and exposes its residual uncertainty/limited flag. Neither the tests nor the graphical evidence claim one-metre accuracy in every capped case.

A fresh uncapped 1280×720 Training-map capture after these fixes records 600 focused frames: frame p95 **3.62 ms**, maximum **4.00 ms** (`build/environment-smoke/fixes-training-performance.json` and `.json.png`). This is the same single-window CPU wall/presentation measurement, with no active Consoles; it does not measure isolated GPU time or establish six-display acceptance.
