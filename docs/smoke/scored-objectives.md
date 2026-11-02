# Scored-objective journeys

These are explicitly invoked production-path journeys. They record behaviour and
timing without making shipped balance or historical mission outcomes mandatory
automated assertions. The implementation and manually assessed journeys are
recorded below. Platform validation has its own explicit status.

Build the native runners:

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_objective_smoke,phoenix_craft_smoke,phoenix_combat_smoke
```

The headless runner claims no Consoles and submits no human work. It records the
public objective board, each eligible Controller's permitted full diagnostic
snapshot and physical checkpoints through production save/restore:

```powershell
./build/native/Release/phoenix_objective_smoke.exe build/native/base.zip base.rescue-under-threat base.courier 12000 build/rescue-objectives
./build/native/Release/phoenix_objective_smoke.exe build/native/base.zip base.asteroid-duel base.courier 2400 build/combat-objectives build/native/test-missions.zip
```

An optional seventh argument is an ordered child archive (`-` for none); an
eighth argument resumes a checkpoint. Output uses `.trace.jsonl`,
`.checkpoint.json` and `.state.json`. Creating `<output-prefix>.stop` ends the
run at its next 200-tick checkpoint and preserves final state. Use a fresh output
prefix for every independent run.

The graphical carrier runner selects its authored manual profile through normal
session setup, explicitly authorises departmental contribution, assigns a team
through its Teams page, selects transport and submits Bay orders through visible
Console controls. Flight does not choose recall. Native synthetic craft
integration separately covers delegated autonomous Bay dispatch and recovery.

```powershell
./build/native/Release/phoenix_craft_smoke.exe build/native
./build/native/Release/phoenix_craft_smoke.exe build/native ship
./build/native/Release/phoenix_combat_smoke.exe build/native
```

The rescue replay exposed an omitted optional speed constraint making accepted
courses infeasible. The Controller regression now covers the no-limit default,
and the corrected replay executes ordinary scored propulsion work. Oversized
candidate diagnostics also exposed the provider context cap: bounded snapshots
now retain selected reasons and explicit omission counts; tools can request all
rows without changing authority or evidence.

These runs do not establish six-display performance, physical input-device or
screen-reader acceptance. Inspect the generated captures separately from the
physical state and Controller decision records.

## Migration acceptance record — 1 November 2026

### Comparison with the agreed implementation sequence

| Plan step                   | Delivered result                                                                                                                                                       |
| --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Contract and selector audit | Three passes, seven tiers, report/local-information rules and classified native-selector inventory.                                                                    |
| Shared arbitration          | Compiled policies and candidates, deterministic utility, persisted commitment identity, ordinary executor/preemption and score/reason diagnostics.                     |
| Evidence and messaging      | Frozen permitted inputs, recipient-scoped retained evidence, revisions/expiry/withdrawal, matching typed claims and separate acceptance/execution/completion receipts. |
| Behavior migrations         | Repair/Medical, craft/away preparation, scanner/point defence, combat/retreat and interdictions use the shared admitted path; superseded selectors are removed.        |
| Captain and persistence     | Revisioned multiple marks through human/AI admission, native/browser projections and controls, provider inputs and schema-50 continuations.                            |

The audited areas are covered by the same mechanism: interdiction goals,
evidence-based combat/pursuit/retreat, trajectory-based interception, known repair
assignments, local Medical triage, Bay-owned craft orders, departmental away
preparation, disclosed scanner candidates and scoped readiness facts. Scenario
goal usefulness remains an authoring question when no supported binding exists;
diagnostics expose that refusal rather than discovering hidden work.

The full native gate passes all 53 system tests, native continuation recording
and network services (`build/scored-final-native-gate.log`). Following the
portable-recovery memory correction, native Session integration passes again
(`build/scored-final-session-native2.log`), and the refreshed network service gate
passes all 12 checks (`build/scored-final-network-recovery.log`). These include
scoped inference, fleet recovery, Console routing, operator replacement, TLS and
pre-game admission. All ordinary Release WASM integrations pass in
`build/scored-final-web-release-gate.log`, including large portable recovery and
the migrated operations. The final comparison initially refused an old extra
`build/native/ftl-navigation-continuation.trace` before comparing fingerprints.
That historical artifact is preserved in `build/scored-preserved-traces/`.
Rerunning the unchanged production comparator passes all 23 native/WASM
continuations (`build/scored-final-parity.log`, exit 0). The complete WASM test
run was retained; no integration or trace case was omitted or weakened.

Synthetic production-loader integrations cover arbitration, recipient isolation,
report lifecycle, team local knowledge and home return, departmental preparation,
physical operations, handover and checkpoint continuation. They use test-only
fixtures rather than shipped performance assertions. The selector inventory in
[the implementation contract](../scored-objectives.md#native-selector-migration-inventory)
classifies the retained factual calculations and physical execution mechanisms.

Independently packed and verified production archives:

| Archive                                | SHA-256                                                            |
| -------------------------------------- | ------------------------------------------------------------------ |
| `build/scored-final-base.zip`          | `3d20beade7bcf5a6f232a44d489e5b130553cb32247e5a469e5af64b2bf94104` |
| `build/scored-final-test-missions.zip` | `a2445b759b3083fa3e098e31955a3e378b93e359dd6f89b5a0e3cd63d7ebfdfc` |
| `build/scored-final-test.zip`          | `1e97a96e886da5e7603bfd548f33d6ba95fba46e8f1f3a46b228322493ace312` |

Verification logs are `build/scored-final-base-verified.log`,
`build/scored-final-child-verified.log` and `build/scored-final-test-verified.log`.
These working-tree archives include the pre-existing unrelated content/build
changes; those changes were preserved separately from migration commits.

### Explicitly invoked journeys

- **Combat:** the all-AI Courier asteroid-duel run records 2,400 ticks in
  `build/scored-final-combat-observed.log` and the matching state/trace files.
  The observed raider is destroyed; the Courier survives after 50 shots.
  These are descriptive results, not balance assertions. No human work was
  injected into the headless run.
- **Carrier:** `build/scored-carrier-observed-hostile.log` passes the graphical
  shuttle-profile journey: physical delivery, release, replacement pickup,
  defence, escort, bomber ammunition expenditure, explicit Bay recall,
  servicing and FTL recovery. Final `build/carrier-serviced.png` and
  `build/carrier-ftl-recovered.png` were visually inspected. This receipt does
  not claim a fresh second ship-transport variant. Autonomous dispatch and
  recovery also pass synthetic integration independently of human UI orders.
- **Rescue/delivery:** `build/scored-final-rescue-progress.log` records the
  resumed all-AI journey through tick 6,201. Departmental requests prepare
  repair, Medical, Security and specialist contributions. Admitted docking and
  physical cargo work deliver 100 station supplies; delivery objective 6 is
  completed, with no cargo requests or batches remaining. The repair mission
  remains open. The remaining rescue narrative has `observe`/grading goals
  without supported action/navigation bindings; this is evidence for the
  separate scenario-objective design review, not a completed rescue claim.

The rescue checkpoint uses preserved `build/scored-contract50-base.zip`
(`95b52c14ab7c2cc61e00f49f97033823fb5935977c88cbb1c3a5d3bfdcd53495`)
for exact content compatibility. Its runtime contract is the same schema 50;
later craft-contact tuning does not affect that journey. Old archives and
checkpoint files remain intact.

Runtime and portable sessions now require schema 50 and messaging kernel 6.
Incompatible earlier sessions require a new session; existing files are
preserved. Captain marks, recipient evidence, action commitments and private
continuations persist through supported checkpoints.
