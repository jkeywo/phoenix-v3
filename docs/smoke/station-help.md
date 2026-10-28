# Station-help verification — 28 October 2026

Verified the shared working tree based on `10d553d`, including the station-help
changes in the commit containing this receipt. Build outputs use
`build/station-help-native` and `build/station-help-web`.

## Automatic and content checks

- Native application build and `native.exe --build-check`: pass.
- WebAssembly client and `phoenix_content_check` builds: pass.
- Native `system.content-and-packs` and `system.lobby-worker-consoles`: pass.
- WebAssembly `phoenix_content_check.js`, executed under bundled Node from its
  build directory: pass.
- Production base and test-mission packs, including restricted client companions:
  verify through the production packager.
- Targeted clang-format, gersemi, Prettier and whitespace checks: pass.

Synthetic content cases cover inherited help, fully qualified nested page IDs,
exact workload selection, explicit defaults for unprofiled fixed views, required
coverage, invalid references and public client transfer. Lobby cases check current
help publication and refresh after replacing content. No shipped wording or tuning
values are frozen as system-test goldens.

## Explicit graphical journey

Command, from the repository root:

```powershell
./build/station-help-native/Release/phoenix_station_help_smoke.exe `
  build/station-help-native/base.zip `
  build/station-help-native/test-missions.zip
```

Pass: **eight stations and 67 tab-help popups**, across all supported complexities.
The journey uses the real local session, production packs and Console renderer,
including granted auxiliary tabs. It checks readable help, viewport bounds,
keyboard focus containment, Close/Escape dismissal and focus return.

Windows / raylib GLFW / NVIDIA GeForce RTX 5090 Laptop GPU, OpenGL 3.3;
1280 × 720 window and Console region. Actions use the ordinary application
command path and injected RmlUi keyboard events. Representative captures are in
`build/station-help-captures`; Helm and Away-mission captures were inspected for
layout and readability. Captures are taken before swapping the window buffers.

## Limits

This is graphical and programmatic evidence. It does not establish physical
screen-reader, touch/gamepad or six-display acceptance, browser GUI acceptance or
a frame-rate target. Lobby introduction selection and replacement are covered by
synthetic projection checks; the client lobby uses its existing scrolling card.
