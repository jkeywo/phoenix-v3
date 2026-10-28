# Native screen device verification

28 October 2026. This receipt covers saved screen audio/controller assignments and the native miniaudio adapter. The [behavior contract](../screen-devices.md) describes routing, persistence and defaults.

## Native checks

- The native executable, input/semantics, menu-library and screen-audio targets build successfully. `--build-check` passes (`build/screen-devices-final-native.log`, `build/screen-devices-build-check.log`).
- The three focused CTest suites pass: `system.input-and-semantics`, `system.native-menu-library` and `system.screen-audio` (`build/screen-devices-focused-tests-pass.log`). They cover saved identities, reconnects, controller ownership, ambiguous hardware/remembered identities, path-only restoration, complete neutral gating, standalone Console routing, output choices, older indexes and persistence across content bases.
- The audio suite opens two synthetic miniaudio null endpoints through the production decoder/device/engine implementation. WAV and Vorbis playback, simultaneous outputs, endpoint sharing, independent stop and silent missing/muted outputs pass. These assertions do not establish audible physical playback.

## Browser checks

The threaded WebAssembly client and `phoenix_input_check` targets build successfully with the workspace's configured development linker (`-O0`; Release C++ compilation optimisation is unchanged). The input executable passes under the bundled Node runtime (`build/screen-devices-web-client.log`, `build/web/screen-devices-wasm-input.log`). The browser retains raylib playback and omits native device selectors. This is focused client/input validation; the complete host/Workshop release gate was not rerun.

## Explicit graphical and accessibility checks

The existing test-only GUI fixture was regenerated with `python tests/manual/gui_fixture.py`. The explicit `--gui-navigation-smoke` run passes all nine stages in a 3840 × 720 window containing two Console regions and a Viewscreen (`build/screen-devices-gui-final.log`). Its isolated profile is `build/screen-devices-gui-final`.

The Audio panel lists the detected TV, headset, onboard speakers and display outputs alongside System default, Muted and Test sound. Mute/default selection and Controllers pairing/open/close actions pass through ordinary semantic/controller navigation. The two captures, `build/screen-devices-console-audio.png` and `build/screen-devices-console-controllers.png`, were visually inspected. The second Console remains visible while the first has settings open.

The explicit Windows platform probe passes its UIA label/bounds/focus/invoke-queue checks (`build/screen-devices-platform.log`). This is platform semantic evidence, not actual screen-reader operation.

## Corrections and remaining evidence

The first audio test used a fixture path relative to its CMake subdirectory; the target now uses the project-root fixture directory. The first graphical launch caught duplicate existing English translation keys; those duplicates were removed before the passing rerun. A missing generated sensor object blocked an earlier native link; rebuilding that existing library repaired the build without changing its source. Earlier failed logs remain separate from the passing receipts.

SDL reported **zero physical gamepads**, and the pointer backend reported no touch contacts. Physical two-monitor assignment, audible endpoint identification, controller unplug/replug and real screen-reader acceptance remain outstanding. The graphical run uses virtual regions and does not establish six-display hardware/performance acceptance.
