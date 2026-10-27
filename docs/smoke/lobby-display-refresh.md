# Live lobby monitor inventory

27 October 2026. Windows and the native backend both reported three attached displays: a 3840×2400 primary, a 1920×1080 screen at x=-1920, and a 1920×1080 screen at x=3840. The native lobby previously enumerated displays only at startup, so a later connection change could leave a stale screen list.

The native pre-game loop now polls the monitor inventory twice per second. The shared update path preserves physical monitor identity, remaps local participant routes when indices change, releases a disconnected route without forgetting its saved station, and invalidates the one-time restoration guard so reconnecting can restore the station. Empty transient enumerations are ignored. Active gameplay and explicit/virtual test layouts retain their existing routing.

Focused system checks passed 2/2 (`build/lobby-display-refresh-tests.log`): input/semantics and the real asynchronous local-console worker. The synthetic worker fixture covers adding a third display after a failed restoration, unchanged polling, re-enumeration with preserved operator name, disconnect and reconnect. Reconnect waits for asynchronous preparation to settle, as the actual lobby does.

The focused native journey `--lobby-monitor-list-smoke --local-only --data build/lobby-display-refresh-profile` passed in `build/lobby-display-refresh-final.log`. It injected a stale two-monitor inventory, observed `Native displays refreshed: 3 attached`, and verified two extra-screen choices for the station. `build/menu-native-lobby-displays.png` was visually inspected: Helm and Support both show S2 and S3. Windows display settings were not changed. This verifies recovery from a stale inventory; physical unplug/replug remains a device acceptance check.

The final VS Code executable `build/native/Release/native.exe` rebuilt successfully and passed `--build-check` (`build/lobby-display-build-check.log`). The broader pre-existing menu smoke stopped at its unrelated invalid-scenario assertion; it is not claimed as passing evidence for this change. The focused monitor journey skips that unrelated scenario-error exercise and uses an isolated profile.
