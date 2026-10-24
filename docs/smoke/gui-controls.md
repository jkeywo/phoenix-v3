# GUI controls verification

Manually triggered evidence recorded on 24 October 2026 for the controller navigation, text scaling, Workshop/GM presentation, browser Redo and Windows accessibility changes. This receipt does not establish the remaining physical-device or full display acceptance requirements.

## Automated checks

| Check                                                                 | Result       | Local receipt                        |
| --------------------------------------------------------------------- | ------------ | ------------------------------------ |
| Native CTest suite                                                    | 40/40 passed | `build/gui-native-final-tests.log`   |
| HTML launcher tests                                                   | 29/29 passed | `build/gui-final-launcher-tests.log` |
| WASM input, build, content and disposable Workshop integration checks | 4/4 passed   | `build/gui-isolated-web-tests.log`   |
| WASM server, client and Workshop links                                | Passed       | `build/gui-isolated-web-final.log`   |
| WASM Workshop test worker build                                       | Passed       | `build/gui-browser-worker-build.log` |
| Native Windows UIA COM probe                                          | Passed       | `build/gui-final-platform.log`       |

The native presentation library also compiled after the Console authority cancellation refinement (`build/gui-ownership-build.log`). The final controller selection cleanup compiled into all three WASM applications.

Other work was changing content, saves and simulation in the shared checkout during verification. The final WASM build therefore used an isolated archive of GUI commit `68e2a7c`, the exact refinements from `6d48f36`, and the final two-line controller selection cleanup. Its source and output are in `build/gui-verify-source` and `build/gui-verify-web`. These results do not assert a complete build of the concurrently changing checkout or a full native/WASM continuation gate.

## Native graphical journeys

Generate the synthetic fixture after building the packager:

```powershell
python tests/manual/gui_fixture.py
```

The fixture is derived only from test data. Its `two-regions.json` contains two independently routed Console regions and one viewscreen, each 1280×720, in one 3840×720 window. It does not inherit shipped ships or balance packs.

Run the manually enabled checks from the repository root:

```powershell
./build/native/Release/native.exe --base build/gui-fixture/gui.zip --local-only --mission test.map --slot test.slot --ship test.controllable --display-layout build/gui-fixture/two-regions.json --gui-navigation-smoke --data build/gui-evidence-navigation
./build/native/Release/native.exe --base build/gui-fixture/gui.zip --local-only --workshop --tools-presentation-smoke --data build/gui-evidence-tools
./build/native/Release/native.exe --platform-check --local-only --data build/gui-evidence-platform
```

The navigation run passed traversal, numeric commit/cancel, Settings, text scaling and Flight/UI mode checks through the rendered Console surfaces. It also checked independent Console selections and preservation of keyboard focus. The capture `build/gui-controller-smoke.png` was visually inspected. Receipt: `build/gui-final-two.log`.

The tools run passed font/style checks at 75%, 100%, 150% and 200%, reset to 100%, pseudo-localisation, accented Latin glyph availability and literal message parameter preservation. Workshop at 150% and read-only GM at 100% were visually inspected in `build/gui-tools-150.png` and `build/gui-tools-100-gm.png`. Receipt: `build/gui-final-tools.log`. The latest capture timing adjustment was compiled in the isolated WASM build; the earlier 200% capture is not evidence of the GM screen.

The Windows COM probe checked labels, bounds, focus and queued invocation, including provider lifetime across unrelated insertion/reordering, removal/recreation and session replacement. This is programmatic UIA evidence, not an audible screen-reader journey.

## Browser keyboard journey

The isolated WASM Workshop was served on loopback with COOP/COEP headers and loaded in the in-app browser at 1280×720. A raw synthetic `gui-browser.zip` fixture was used because a pack produced by the concurrently changing native packager did not match the archived loader.

Using actual browser pointer and keyboard input, Add map changed `test.map` to `test-core.map1`; Ctrl+Z restored `test.map`; Ctrl+Y restored `test-core.map1`. A second Undo followed by Ctrl+Shift+Z also restored `test-core.map1`. The visible result was captured in `build/gui-browser-redo.jpg`. The temporary tab and loopback server were closed after the check; no draft was saved.

The old synthetic fixture displayed an unrelated “unknown Controller tree” authoring diagnostic. This journey verifies the browser history shortcuts, not clean authoring or mission execution for that fixture.

## Acceptance limits

- Controller events in the graphical smoke are injected through the application navigation path. Physical gamepad hotplug, touch and an audible Windows reader journey remain manual checks; no controller or touch device was detected on this run.
- The test window was 3840×720 on a machine reporting a 1920×1080 primary display and an NVIDIA RTX 5090 Laptop GPU. Six physical 1080p displays at 60 Hz were not measured.
- English and pseudo-localisation with Latin font coverage were checked. Complex-script shaping, bidirectional editing and additional translated languages are outside this delivery.
- Workshop/GM use pointer and keyboard navigation. Arbitrary player text entry requires a physical or platform keyboard. Phone layout redesign and Linux screen-reader integration are separate requirements.

See [GUI controls and presentation](../gui-controls.md) for the supported bindings and presentation settings.
