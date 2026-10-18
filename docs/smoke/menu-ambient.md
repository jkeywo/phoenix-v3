# CRT flicker and scutter evidence

19 October 2026, Windows native and embedded Chromium, 1280 by 720. These are explicitly invoked graphical/timing checks, separate from automated system tests and physical-device acceptance.

## Observed

- The new robot sprite is side-on, with a single visible drive wheel and its gripper facing the direction of travel. A natural HTML crossing moved rightward across the foreground floor, behind the controls. DOM observations estimate a 15.59s complete crossing, within the slow band; the avatar/name flickered while the housing stayed steady. Samples are in `build/ambient-web-observations.json`; captures are `build/ambient-web-scutter.png` and `build/ambient-web-flicker.png`.
- Settings and Reduced Motion restore full CRT screen opacity and hide the robot. Focus returns to Settings when closing the popup. Decorative movement adds no keyboard or screen-reader targets. The CSS also suppresses these effects for the browser's reduced-motion preference.
- An isolated native preview uses a copy of the compiled application/assets with accelerated decorative timer metadata in ignored build output. It captured the side-on robot on the floor and the dimmed CRT avatar/name at the same time (`build/ambient-native-menu.png`, `build/ambient-native-capture.log`). Source/shipping intervals remain 15–40s and 40–60s. Preview intervals were 0.5–1s for flicker and 2–3s for crossings, with shortened speed bands; this verifies rendering rather than real-time interval acceptance.

## Checks

Native and all three browser-role builds pass (`build/ambient-native.log`, `build/ambient-web.log`). The normal native cascading-menu/popup/readiness/Settings/save/quit journey passes with the production metadata (`build/ambient-smoke.log`). The eight existing browser shell cases pass (`build/ambient-shell-tests.log`). Repository formatting and whitespace checks pass (`build/ambient-format.log`, `git diff --check`).

A bounded probe runs the production C++ cosmetic-clock implementation in WASM with fixed test seeds, using the scene metadata embedded in its filesystem. It exercises both directions, both speed bands, opacity dips and restoration/hiding when disabled (`build/ambient-probe.log`). It does not advance gameplay. Browser live timing uses freshly seeded cosmetic streams; the probe's repeatable seeds are only for checking selections.

No public deployment, physical screen-reader/controller or six-display performance claim is made by this receipt. Generated sprite provenance is in `content/ui/menu/scutter-generation.json`.
