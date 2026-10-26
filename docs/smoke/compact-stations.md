# Compact station verification

Date: 25 October 2026.

The manually invoked `phoenix_console_transition_smoke` passed the Courier and Cruiser station journeys with a 4K host and two routed 1080p surfaces. The journey uses the real native renderer, measured layout, pointer handling, admitted commands and assigned viewscreen. It is not registered with CTest.

The checks cover the top-row header, compact weapons and selected target, persistent Dispatch, capability-filtered tabs, isolated Navigation and Comms, contextual waypoint selection, and radar/Navigation/conversation sharing followed by return to the camera. Engineering checks one-star read-only power, three-star allocation through physical authored groups, compartment/team details and large-fitting overflow handling. Main components fit without scrollbars. The Courier journey also checks throttle capture, zero deadzone, All stop and 125% text. Captures were visually inspected, including Cruiser Engineering and the actual shared radar on the host.

Evidence: `build/compact-journey-11.log`, `build/compact-cruiser-engineering.png`, `build/compact-shared-radar.png` and the other `build/compact-*.png` captures. These checks inject input programmatically; physical touchscreen, screen-reader and six-display performance acceptance remain separate device checks. This receipt does not establish acceptance of the concurrently revised flight model.

The final `phoenix_layout_probe` journey passed header editing, preview star changes, export/reopen and shared runtime geometry (`build/compact-workshop-final.log`). Focused system runs passed controller runtime, content/export, input/semantics, network transfer, Medical, directed behavior, combat and native menu/library checks (`build/compact-focused-final.log` and `build/compact-ui-final-tests.log`). Combat covers hostile, friendly, neutral and unidentified targets, overrides, live identification/disposition changes and resource conservation when offensive fire is refused.

VS Code launches `build/native/Release/native.exe` after its native build task. An actual local-profile launch completed content verification in `build/compact-installed-final.log`. The installed `phoenix-base` hash matched the shipped `Release/assets/base.zip` hash, `7db1d4dd5bfff41b07e5f81f5ca9dc16d5a1676a1b2c22d8937b83ae5b8a74f0`. The separately installed, disabled `phoenix-test-missions` pack retained its previous hash and state. Synthetic menu tests also cover upgrading an older base without a backup and preserving independent mods. Existing saves retain embedded content.

## Helm contact follow-up

Contact details now remain inline, with motion fields sharing a row and blocked scan progress omitted. The left column gives target information more room; weapon overflow reserves its popup button only when needed. System status sits beside the radar. The final native journey passed normal and 125% text, mixed-resolution station geometry, and the existing operational controls (`build/helm-contact-final-journey.log`).

The explicit near-planet journey relocates only the disposable scenario's starting slot. Cairn's centre is outside the plotted range while its near surface is inside; its visible circumference can be selected, its inline range reads 2300 m, and the panel fits at 1080p. The capture `build/helm-planet-surface.png` was visually inspected. Run this focused journey with `phoenix_console_transition_smoke.exe build/native --planet-only`. Circular footprints travel through the ordinary permitted network/shared-display projection. The authored combat radar includes ordinary contacts, and the planet's collision radius is now 700 m, up from 650 m.

The broader native integration run passed 43 cases; its old centre-distance boundary fixture was then updated to the new surface-distance contract and passed. The final four-case regression passed sensors, streamed asteroid fields, combat and motion/radar boundaries (`build/helm-contact-final-regressions.log`). It includes safe contact-arc sensing for generated inert bodies without machinery. Production pack verification and the native build check passed. The actual installed-game launch completed content verification in `build/helm-contact-installed.log`.

## Viewscreen condition frame

The frame, raised condition panel and condition label share one geometry definition. Border slices scale with display height instead of retaining their source-pixel corner size. The label uses matching physical bounds and fits inside the panel at 1080p and 4K; narrowing the scene for a sidebar preserves its text/frame scale.

The explicit native `--viewscreen-only` journey waits for settled gameplay after asset loading and resizing, then measures the condition label inside the actual rendered panel and captures both resolutions. `build/viewscreen-condition-settled-journey.log` passed, and `build/viewscreen-condition-1920.png` and `build/viewscreen-condition-3840.png` were visually inspected. The Controller integration check passed in `build/viewscreen-condition-integration.log`.

The viewscreen no longer turns transport omission metadata or its local eight-entry history cap into gameplay readouts. “Entries omitted from network view” described entries omitted from a bounded status/history projection, not ship damage; intentionally hidden objective/waypoint records could also increase that count. Transport limits and retained histories are unchanged. Synthetic semantic checks cover suppression of that misleading diagnostic alongside the existing objective/waypoint exclusions.

## Support visual controls follow-up

The manually invoked `phoenix_console_transition_smoke.exe build/native --support-only` checks the real Courier Support layout at 1920×1080, then enlarged text. It verifies the fitted camera widgets update authoritative camera state, the numbered Condition slider selects authored conditions 1, 5 and 3, and the visible shield canvas contains the owned ship sprite and observed pools. It checks Prepare precedes disabled Depart and changes to Cancel, and checks the Comms history uses the available width and vertical area. Empty optional conversation sections release their authored space. The main Support panel fits without scrollbars; conversations retain scrolling for long histories and rosters.

Final evidence: `build/support-final-journey.log`, `build/support-visual-controls.png`, `build/support-visual-controls-large-text.png` and `build/support-comms-compact.png`. Screenshots were visually inspected for side-on orientation, percentages beside the arcs, a grey Depart button, readable condition ticks, compact message spacing and aligned composition controls. Focused controller-runtime, input/semantics and content/export regressions are recorded in `build/support-final-regressions.log`; the controller fixture covers selected-conversation optional-slot availability. Physical touch and screen-reader acceptance remain device checks.

The native `--build-check` passed (`build/support-final-build-check.log`). A real local-profile launch of the VS Code executable completed content verification (`build/support-controls-installed.log`). Its installed `phoenix-base` hash matches the shipped archive, `6208cfb00f863a91312c442437d860074adcf068fd57980c256cf0b3030cd842`. The separate disabled `phoenix-test-missions` pack remains installed with its previous hash.
