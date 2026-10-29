# Settings and menu verification

29 October 2026. Settings now close with a top-right X, native audio outputs use a dropdown, Viewscreen sidebar choices live under Display, and the native profile retains its CRT artwork across button states.

## Passing checks

- Native and the focused menu/input test targets build successfully. `native.exe --build-check` passes. Build logs are `build/settings-native-final.log`, `build/settings-native-bridge-build.log` and `build/settings-focused-build.log`.
- `system.screen-audio`, `system.native-menu-library` and `system.input-and-semantics` pass. These protect independent screen routing, saved/disconnected output behavior and existing input/semantic contracts.
- All 38 browser launcher checks pass (`build/settings-launcher-tests.log`). Changed C++ and web/document files pass their scoped formatting checks.
- The explicit synthetic two-Console/Viewscreen `--gui-navigation-smoke` passes all nine stages (`build/settings-gui-smoke.log`). Its controller journey opens the Audio dropdown, selects Muted, restores System default and closes Settings. The fixture was materialised with the existing test-only `materialise_fixture_widgets` helper before production packaging; the older `gui_fixture.py` output alone still uses retired layout schema 1.
- Visually inspected the final 1280×720 native Audio/Profile dialogs, centred X and restored CRT profile artwork, plus the 3840×720 two-Console Audio capture. Captures are `build/menu-native-settings.png`, `build/menu-native-settings-profile.png`, `build/menu-native-host.png` and `build/screen-devices-console-audio.png`.

## Incomplete broader journeys

The full `--menu-smoke` passes its settings, keyboard/UIA range and saved-output checks, then stops at the existing asynchronous scenario assertion `Invalid scenario unexpectedly loaded` (`build/settings-menu-final.log`). It does not establish the later lobby/save/quit steps.

The 1920×1080 `--bridge-ui-smoke` stops before opening Settings because `Condition must have rendered bounds` fails (`build/settings-bridge1920.log`). Its updated Session/Display and sidebar close/reopen assertions therefore remain unexecuted. A subsequent Viewscreen-only layout attempt was refused by the existing requirement for at least one Console region (`build/settings-bridge-explicit.log`).

Physical audio endpoints, physical controllers, actual screen readers and six-display performance were not tested. Browser device routing remains unchanged.
