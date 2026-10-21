# Mission objectives and debrief verification — 21 October 2026

This receipt covers the generic objective/outcome increment and the shipped rescue. It does not establish balance, physical multi-display performance or actual screen-reader operation.

## Integration and compatibility

- The final shared-tree native gate passed all 30 system cases, content verification and eight signalling checks. A stable mission snapshot also passed its 29 system cases.
- The WASM mission, away, FTL, scenario, scenario-World, knowledge and Controller suites passed.
- Native/WASM mission continuation matched across transition delivery, partial FTL charging, transport-phase losses, simultaneous departures and report creation. Existing away/FTL suites cover interruption, repair preservation, refusal and recovery.
- Synthetic fixtures use production loading and execution, independently of shipped ships and tuning. They check Missing values, typed comparisons, failure precedence, latched completion, scoped events, exactly-once loss, report privacy, authoritative facts, freezing and multi-ship isolation.
- World remains schema 13. Scenario/away continuation versions are local, preceding compatible records are accepted, content identity checks remain, and versionless departure reports are preserved.

The broader shared working tree acquired separate carried-craft changes during implementation. Final mission validation used `build/mission-source`, `build/mission-native` and `build/mission-web` so those edits remained intact. The final shared-tree native gate and graphical journeys include those changes. WASM evidence covers the stable mission snapshot; it is not recorded as a passing full shared-tree WASM gate.

## Explicit graphical journeys

The `phoenix_mission_smoke` target is excluded from CTest. It drives the real Application, RmlUi actions and ordinary owned Controller commands, with fixed simulation ticks between rendered frames. It does not award repairs, grant charge, teleport parties or remove the casualty through a scenario shortcut.

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_mission_smoke
./build/native/Release/phoenix_mission_smoke.exe build/native success
./build/native/Release/phoenix_mission_smoke.exe build/native partial
./build/native/Release/phoenix_mission_smoke.exe build/native loss
```

For browser evidence, build the same target for WebAssembly, serve `build/web`, and open `phoenix_mission_smoke.html?mode=success` (or `partial` / `loss`). The browser harness yields between frames and retains a debrief image after closing the native graphics context. Its optimization and async settings apply to this manual target.

| Journey                                                                                    | Observed result                                                                                                   | Native | Browser |
| ------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------- | ------ | ------- |
| Complete services and propulsion work, recover the party, wait for casualty escape, depart | Success; both restorations true, casualty departed, participating officer/team recovered                          | Passed | Passed  |
| Complete services, recall before propulsion restoration, depart                            | Partial; services true, propulsion false, casualty active, party recovered; remaining goals unfinished            | Passed | Passed  |
| Deploy the party, destroy the casualty through actual weapon fire, depart                  | Failure; casualty destroyed, team/officer lost, current restoration false; earlier accomplishments stay completed | Passed | Passed  |

Native screenshots are written to ignored `build/mission-success.png`, `build/mission-partial.png` and `build/mission-loss.png`. Browser captures appear below the canvas and were saved as `build/mission-browser-success.png`, `build/mission-browser-partial.png` and `build/mission-browser-loss.png`. Console routes 1 and 2 receive the same frozen crew debrief. The semantic projection exposes the grade, explanations, objective statuses, fact values and personnel dispositions after departure.

## Corrections found during the journeys

- The casualty needed an authored reactor command rather than relying on an initial stored-energy reserve. Its FTL policy still charges through ordinary supply/cooling and waits for visitors and transfers.
- The readiness event is declared and subscribed by the rescue conversation; its short authored notice period allows delivery before departure.
- The report uses the actual Console region for RmlUi layout, and departure is distinguished from physical destruction in the combat projection.
- An empty departure batch no longer creates an objective boundary before physical changes. A synthetic same-tick destruction case verifies failure takes precedence.
- The loss journey manoeuvres to expose remaining compartments instead of expecting repeated fire from one angle to destroy the whole hull.

These are manually invoked receipts, not production-content timing goldens. No six-monitor hardware or actual assistive-technology session was available in these runs; browser semantic inspection is narrower evidence.
