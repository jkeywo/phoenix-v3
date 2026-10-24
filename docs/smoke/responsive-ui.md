# Responsive Workshop UI verification — 24 October 2026

The authoring and runtime implementation is described in [Console layouts and themes](../responsive-ui.md).

## Native checks

An isolated Visual Studio Release build under `build/layout-native` built `phoenix_layout_probe`, `phoenix_content_tests`, `phoenix_system_tests`, `phoenix_transfer_tests` and `phoenix_pack`. The isolated directory avoids sharing generated C++ module files with other ongoing development builds.

The three integration executables passed. Synthetic fixtures enter through production pack loading and verify reusable layouts/groups, theme resolution and artwork transport, compact component identity, tab selection/revocation, Workshop rename/history/export/reopen, preserved theme inheritance, held-input neutral release, read-only command admission and independent Console selection on separate surfaces. Existing semantic-admission checks also pass.

The production packager verified `content/base`, `content/test` and `content/test-3d`, including their migrated layouts, compact HUD layouts and themes. These checks validate authored content structurally; no shipped geometry or ship performance is frozen in a golden test.

## Explicit graphical journey

Invoked `build/layout-native/Release/phoenix_layout_probe.exe build/layout-smoke-source` with a disposable synthetic pack. This is a development probe, excluded from CTest. It drives the actual Workshop workspace and production Console preview, edits the draft, verifies it, exports an archive and reopens it before continuing.

The completed journey captured and visually inspected:

- Desktop Helm radar, throttle and operational panel placement (`build/layout-workshop-20.png`).
- A 390×844 phone preview with compact page tabs (`build/layout-workshop-80.png`).
- Enlarged text at 150% with local theme changes and scrolling (`build/layout-workshop-115.png`).
- Separately composed conversation directory, history, replies and composer, including an unavailable action (`build/layout-workshop-150.png`).
- Engineering cards (`build/layout-workshop-185.png`).
- Navigation display and its existing controls (`build/layout-workshop-220.png`).

Preview commands remain inside the isolated sample session. The probe edits the draft programmatically; the captures establish rendering and round-trip evidence, not a claim that a person exercised every drag, keyboard or touch operation.

## Browser checks and limits

The WebAssembly `client`, `workshop` and `phoenix_content_check` targets built successfully in `build/layout-web`. Running `node phoenix_content_check.js` from that directory passed the same synthetic content suite. The launcher suite has 30 passing tests, including generated theme styles, font fallbacks and rejection of executable style values or remote theme assets.

Physical multi-display routing, real touch/gamepad hardware, screen-reader usability and six simultaneous 1080p surfaces at 60 Hz remain separate smoke evidence. This receipt does not claim those acceptance requirements are complete.
