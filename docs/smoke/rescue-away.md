# Rescue away priorities — 23 October 2026

The authored scope is described in [Rescue away mission](../rescue-away-mission.md). It continues the established delivery opening and preserves separate delivery, crew and ship-rescue outcomes.

The independently packaged mission content verified as `phoenix-base` revision `3efce5fe3a4c5b374a8bd0f7bd6b9e5268cd1159ef0a41976a33fe8785aac26b`: 34 entity definitions, one map and 20 Consoles. This package excludes the separate Cruiser refit being edited concurrently in the shared workspace.

## Automated checks and review

Native and WebAssembly away, conversation, mission, Medical and sensor integration checks passed against synthetic production-loader fixtures. The new cases cover arrival-based escalation, objective scans and received information, human/AI operator combinations, optional Medical, wounded-first/last/absent outcomes, actual accompanying officer injury/death and absence, interrupted work, recall and saved continuation. Repeated restores do not duplicate work, casualties, messages or report entries.

The native and WebAssembly docking regression checks passed an inertial abeam approach without lateral thrust, including save/resume during guidance. The report tests also verify that an authored event about another ship appears in the player's frozen report, and that report projections survive network serialization.

The requested independent agent review found a Medical-withdrawal exploit and the missing requirement to check the deployed team's continuing availability. That was fixed and reviewed again. The subsequent reviews found no remaining blocking issues in the mission content, saved docking guidance or report event selection.

## Manually triggered journeys

The manual driver uses ordinary Session commands, production-authored content and the Console renderer. It is explicitly invoked and is not a production-content golden or a tuning assertion. Native journeys save arrival and final-report archives for later graphical inspection. The browser page exposes progress and captured frames.

Initial Cruiser testing completed prompt delivery, early reactor work, wounded rescue, propulsion repair, recovery and escape. Courier testing exposed an assisted docking loop against the casualty; the fix was exercised through the actual docking path. Visual inspection also found collapsed report columns, leading to explicit report sizing and separate objective/log and ship/crew columns.

The final Courier journey passed (`build/rescue-native/rescue-away-courier-fixed.log`), resuming its real arrival archive after the earlier completed station delivery. It docked, deployed repair and Medical teams, performed controlled emergency shutdown, rescued all trapped crew, restored propulsion, recovered the teams, undocked, witnessed Morrow's escape and departed. The frozen report independently records **Supplies delivered**, **All rescued**, **Rescue successful**, and Nadia Keene **Severely injured**. The log records the six-minute escalation, shutdown decision, officer injury, completed jobs, recovery and Morrow's escape.

The saved Courier report was rendered again after the layout correction (`build/rescue-native/report-preview.log`). The inspected `build/rescue-courier-report.png` shows readable objectives and timestamped log entries in the main column, with ship compartments and named crew conditions in the side column. The final report archive is `build/rescue-courier-report.zip`. The driver can inspect either an arrival or a departed report archive through its optional checkpoint argument; the browser shell accepts a same-origin checkpoint basename in its `checkpoint` query parameter.

The final fresh Cruiser journey also passed (`build/rescue-native/rescue-away-cruiser-final.log`). Its report records **Prompt delivery — full credit**, **All rescued**, and **Rescue successful**. The reactor was secured before escalation, all three jobs completed, all deployed personnel recovered, and Morrow escaped. No player officer was injured. Its final archive is `build/rescue-cruiser-report.zip`. That run used the earlier report layout; its screenshot is evidence of the journey, not acceptance of the corrected layout. The common corrected report renderer was inspected through the Courier report and reviewed independently.

## Build and remaining graphical scope

The native application build passed after the walkthrough released its audio files (`build/rescue-native/report-game-build.log`). The browser server, client, Workshop and mission-smoke targets built successfully (`build/rescue-web/report-final-build.log`). The final WebAssembly away and mission checks, including the foreign-vessel report event case, passed after that build.

The browser checkpoint page fetched the saved Courier archive successfully, but the concurrently changed runtime rejected it because it requires save schema 20; these journey archives use schema 19. A later native Cruiser report preview also encountered a newly required saved `choir` field. The archives were not altered and ordinary continuation checks were not bypassed. A final browser visual check and a fresh Cruiser capture of the corrected layout remain unverified against that separate in-progress runtime migration. This does not replace the native and WebAssembly synthetic continuation checks or the completed native journeys described above.

Graphical captures and diagnostic runs remain under `build/`, outside the automated fixtures. This work does not establish six-display performance, physical touch/gamepad operation, screen-reader operation or combat balance.
