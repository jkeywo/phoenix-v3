# Scored-objective journeys

These are explicitly invoked production-path journeys. They record behaviour and
timing without making shipped balance or historical mission outcomes mandatory
automated assertions. Final acceptance evidence will be recorded after the
remaining platform and manual runs finish.

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
