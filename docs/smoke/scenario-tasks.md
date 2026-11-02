# Rescue scenario task MVP — 2 November 2026

This receipt separates production content verification, synthetic execution and
partial authored journeys. It does not claim a complete human rescue playthrough,
browser interaction, screen-reader, physical-device or six-display acceptance.

## Implementation checked

The rescue uses a bounded task decomposition whose selected methods run ordinary
scenario state trees. Native production-loader tests cover alternative selection,
Missing evidence, decomposition budgets, dependency ordering, physical completion,
capability-loss repair, independent requests, receipt deduplication, retained
superseded work, exact continuation and transactional rejection of malformed saves.
Scheduling checks group attention by effective human operator and exercise bounded
release windows. Seeded alternatives preserve their setup seed across recovery.

Physical integration cases use synthetic content through production loading:

- Cargo selection depends on an operational compatible shuttle and capacity.
- A spawned damaged target receives a stable disclosed away operation, is repaired
  through ordinary commands, and resumes exactly from a checkpoint. Incompatible
  target overrides are rejected before execution.
- Losing two of three committed pirates selects a feasible one-ship tactic without
  replacing the original roster or replenishing casualties.
- A task child's alternative objective contributes to the original mission's
  departure debrief through explicit scope and `report_as`.
- Portable session recovery uses world/session schema 51.

Workshop authoring tests cover shared task/fact/participant controls, transactional
edits, undo/redo and source ZIP round trips. The shared-control architecture check
passes with 517 used field descriptions. Native and WASM Workshop gameplay/tree
editing and GM diagnostic targets compile.

## Commands and retained evidence

Isolated build directories are `build/scenario-task-native` and
`build/scenario-task-web`. Native executables are under `Release`. This WASM
configuration uses `PHOENIX_BROWSER_THREADS=OFF`; these Node checks do not establish
threaded-browser or graphical browser acceptance.

```powershell
phoenix_scenario_tests.exe
phoenix_away_tests.exe --trace
phoenix_cargo_tests.exe --trace
phoenix_interdictions_tests.exe --trace
phoenix_missions_tests.exe --trace
phoenix_session_tests.exe
phoenix_workshop_cli_tests.exe
python tests/system/workshop_authoring_architecture.py
```

WASM runs use Node and the same synthetic fixtures. Trace capture uses
`scripts/wasm-trace.mjs` to preserve complete output before comparison. Comparisons
normalize platform line endings, parse emitted JSON records, and compare the
interdiction suite's continuation digest directly.

Native logs include `build/scenario-task-compound-native-test.log`,
`scenario-task-authoring-final-tests.log`, `scenario-task-session-final-native.log`
and `scenario-task-missions-final-native.log`. Trace files use
`build/scenario-task-{away,cargo,interdictions,missions}-{native,web}.trace`.

The production pack is generated and independently verified with:

```powershell
phoenix_pack.exe content/base build/scenario-task-mvp-base-final.zip
phoenix_verify.exe build/scenario-task-mvp-base-final.zip
```

Packaging and independent verification pass: 58 entity definitions, one map and
64 Consoles. The archive SHA-256 is
`c86d575ae27be2265699fb0b7ff0052d849b447aea4464380dce2e6c2e300e62`.
An independent source ZIP containing the exact staged definitions also passes the
production verifier (`build/scenario-task-staged-source-verify.log`), so this
result does not rely on unrelated workspace content edits.

The final away trace matches native/WASM as canonical JSON, SHA-256
`176eaf6c4f8e5f5b2832ead06bfc1c1e8a65850a36e301ea6d52737273d9e66a`.
Final native/WASM scenario and portable-session suites pass. Cargo and mission
JSON records and the interdiction continuation digest also match; their hashes
and comparison results are retained in `build/scenario-task-trace-comparison.log`.
Both complete
trace-suite processes exited successfully. These checks include physical
continuation and failure/replanning, rather than only successful compilation.

## Authored journeys and limits

Separate manually invoked unattended AI journeys load the actual Rescue under
Threat pack with Courier and Cruiser. They retain checkpoints and Controller
decision traces under `build/scenario-task-courier*` and
`build/scenario-task-cruiser*`. Courier selects ordinary delivery and the light
pirate branch; Cruiser selects perishable delivery and Screen and Boarder.
Courier reaches distress publication and disclosed repair operations. These
partial journeys were stopped after retained checkpoints to release the disposable
hosts during final regression checks. They establish setup/progression evidence,
not completed delivery, combat, repair, recovery or departure.

The gameplay changes have synthetic physical integration evidence. The complete
shipped rescue remains a manual playtest and tuning gate; no exact shipped-ship
performance or outcome assertions were added to mandatory tests. Opportunity
discovery, general GOAP search and bounty generation remain outside this MVP.
