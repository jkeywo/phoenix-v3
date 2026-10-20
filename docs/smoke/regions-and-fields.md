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

A separate uncapped single-window run of that map collected 600 samples after warmup, at 1280×720: frame p95 **4.85 ms**, maximum **6.17 ms**. This measures CPU wall/presentation time, not isolated GPU execution. The output and framebuffer are `build/environment-smoke/training-performance.json` and `.json.png`. It is not a six-display performance claim.

A later native Navigation Console capture could not run because GLFW/WGL failed to initialise a graphics context (`navigation-markers.log`). Its marker projection and authoring are covered by integration checks, but that attempted capture is not visual evidence. Full Workshop control interaction, rotating relief-specific shadow alignment, visual streaming transitions, physical readers and six-display acceptance remain manual follow-up checks; no headless result is presented as proof of those observations.
