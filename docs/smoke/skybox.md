# Map skybox verification — 20 October 2026

Verified against `174d9ae` plus the skybox implementation. These are manually
triggered smoke results, not production-image goldens or performance acceptance.

## Content and integration

The production verifier resolves all eight maintained maps (three base and five
training). Each explicitly selects
`phoenix-base:assets/skyboxes/settled-dark-v1.png`.
New maps retain the absent-field default, None.

Native `system.content-and-packs`, `system.local-session` and
`system.workshop_test` all pass. The corresponding WASM content, session and
Workshop executables all pass under the bundled Node runtime.

Synthetic content coverage includes absent and empty selections, valid dependency
images, non-string and missing references, corrupt images and wrong dimensions.
ZIP, DAT and scenario export round trips preserve the selection and image bytes.
Clearing a selection survives export; newly created maps remain blank.

Native application and affected test targets build successfully. Browser server,
client, Workshop, content checker and camera smoke targets build successfully,
as do the WASM session and Workshop checks.

Commands used include:

```powershell
./scripts/build.ps1 -Target native -BuildTarget native,phoenix_camera_smoke,phoenix_content_tests,phoenix_session_tests,phoenix_workshop_test_tests -Jobs 1
./.tools/python/cmake/data/bin/ctest.exe --test-dir build/native -C Release -R 'system.(content-and-packs|local-session|workshop_test)' --output-on-failure
./build/native/Release/phoenix_verify.exe build/native/base.zip
./build/native/Release/phoenix_verify.exe build/native/test.zip
```

From `build/web`, run the bundled Node executable against
`phoenix_content_check.js`, `phoenix_session_check.js` and
`phoenix_workshop_test_tests.js`.

## Graphical evidence

The manually invoked camera smoke accepts `skybox` on native and `?skybox` in
`phoenix_camera_smoke.html` on WebGL 2. Both report:

- PASS: skybox viewport, translation, eight headings, poles and None.
- PASS: camera widgets, manual toggle, gamepad navigation, modal admission and
  16 camera renders.

The probe uses the production scene renderer and pack loader. It captures a full
turn in eight headings, both poles, nebula composition and an empty selection.
It checks that camera translation leaves the background pixels unchanged and
that rendering stays inside an inset viewport. Actual Viewscreens also show the
sky behind ships and navigation overlays.

Native captures are in `build/native/skybox-smoke/skybox-*.png` and
`camera-*.png`; the log is `build/skybox-native-smoke.log`.
Run `phoenix_camera_smoke.exe .. skybox` from
`build/native/skybox-smoke` after packaging the native content.

The browser smoke page displays its captures in a gallery. Recorded local
evidence includes:

- `build/skybox-web-viewscreen.png`: in-game Viewscreen.
- `build/skybox-web-ship.png`: foreground ship and navigation effects.
- `build/skybox-web-seam.png`: repaired longitude seam.
- `build/skybox-workshop.png`: Workshop selection.

The browser Workshop picker offered None and compatible pack images. Clearing
and saving reported success, and reselecting the panorama worked. Browser
reload persistence is covered by the WASM production-loader round trip.
Separately, native Workshop cleared and saved a disposable copy of the base
pack; closing and restarting Workshop with that pack showed None and a
successful load/verification message. The saved archive also contained the
cleared field.

## Panorama repair

The original 1774 × 887 generated source remains unchanged at
`output/imagegen/settled-dark-skybox-v1.png`. Initial graphical inspection exposed
a visible longitude seam. A separate imagegen derivative fades the boundary and
polar regions to dark space; final native and browser captures were inspected
for the seam and poles. The runtime image is packaged once in the base pack at
`assets/skyboxes/settled-dark-v1.png`.

The edit prompt and provenance are recorded in
[the runtime panorama record](../../output/imagegen/settled-dark-skybox-runtime.md).
No image goldens were added. These checks do not claim six-display performance,
physical-device or screen-reader acceptance.
