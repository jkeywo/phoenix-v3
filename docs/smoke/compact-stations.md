# Compact station verification

Date: 25 October 2026.

The manually invoked `phoenix_console_transition_smoke` passed the Courier and Cruiser station journeys with a 4K host and two routed 1080p surfaces. The journey uses the real native renderer, measured layout, pointer handling, admitted commands and assigned viewscreen. It is not registered with CTest.

The checks cover the top-row header, compact weapons and selected target, persistent Dispatch, capability-filtered tabs, isolated Navigation and Comms, contextual waypoint selection, and radar/Navigation/conversation sharing followed by return to the camera. Engineering checks one-star read-only power, three-star allocation through physical authored groups, compartment/team details and large-fitting overflow handling. Main components fit without scrollbars. The Courier journey also checks throttle capture, zero deadzone, All stop and 125% text. Captures were visually inspected, including Cruiser Engineering and the actual shared radar on the host.

Evidence: `build/compact-journey-11.log`, `build/compact-cruiser-engineering.png`, `build/compact-shared-radar.png` and the other `build/compact-*.png` captures. These checks inject input programmatically; physical touchscreen, screen-reader and six-display performance acceptance remain separate device checks. This receipt does not establish acceptance of the concurrently revised flight model.

The final `phoenix_layout_probe` journey passed header editing, preview star changes, export/reopen and shared runtime geometry (`build/compact-workshop-final.log`). Focused system runs passed controller runtime, content/export, input/semantics, network transfer, Medical, directed behavior, combat and native menu/library checks (`build/compact-focused-final.log` and `build/compact-ui-final-tests.log`). Combat covers hostile, friendly, neutral and unidentified targets, overrides, live identification/disposition changes and resource conservation when offensive fire is refused.

VS Code launches `build/native/Release/native.exe` after its native build task. An actual local-profile launch completed content verification in `build/compact-installed-final.log`. The installed `phoenix-base` hash matched the shipped `Release/assets/base.zip` hash, `7db1d4dd5bfff41b07e5f81f5ca9dc16d5a1676a1b2c22d8937b83ae5b8a74f0`. The separately installed, disabled `phoenix-test-missions` pack retained its previous hash and state. Synthetic menu tests also cover upgrading an older base without a backup and preserving independent mods. Existing saves retain embedded content.
