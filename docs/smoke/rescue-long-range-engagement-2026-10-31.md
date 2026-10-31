# Rescue with long-range raider engagement, 31 October 2026

Manual native headless run at `5003e71b`, fully AI Courier in
`base.rescue-under-threat`. No human claims or ship commands were submitted.
The full base pack is the same frozen pack as the preceding
[Veil rerun](rescue-veil-disruption-2026-10-31.md). The child pack retains the
previous generic objective/policy corrections and changes the rescue scenario's
combat stand-off from 250 m to 100 m. No additional objectives were introduced.

**The Courier is destroyed.** The raider now uses its existing 1.05 km bearing
coverage to close and fire homing missiles, then uses cannons and Havocs as range
becomes available. The rescue party deploys just before the Courier is lost;
rescue work, recovery, Morrow's escape and the station delivery remain unfinished.

## Observed journey

| Simulation time | Event                                                                                                                                                                  |
| --------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0:11.80         | Station approach completes; distress navigation interrupts the original delivery.                                                                                      |
| 1:34.90         | Distress response objective completes.                                                                                                                                 |
| 1:41.90         | Raider demands withdrawal.                                                                                                                                             |
| 1:46.90         | Refusal leads to scenario-authored hostile posture.                                                                                                                    |
| By 1:48         | First homing missile fired at approximately 540 m separation; raider's normal radar track list is empty. Its assigned target is available only as a permitted bearing. |
| By 2:14         | Raider has fired all 16 homing missiles, closing to approximately 257 m. Cannons have not yet fired.                                                                   |
| By 2:19         | Positional acquisition permits both cannons to open fire, at approximately 207 m separation.                                                                           |
| By 2:25         | Launcher selects Havocs at approximately 153 m separation, within their 157.5 m nominal reach.                                                                         |
| By 2:30–2:32    | Courier completes physical mating and docking at Morrow.                                                                                                               |
| 2:32.90         | Rescue party deploys.                                                                                                                                                  |
| 2:35.50         | Mission ledger records Courier destruction.                                                                                                                            |

Weapon, distance and docking samples have one-second resolution. Objective,
deployment and destruction ledger events retain tick precision. The physical
destruction receipt is tick 3109; the ledger records it at tick 3110.

Final capture at 2:58 records:

- Raider: 16 homing missiles, 74 Havocs, 18 cannon rounds; 3300/3300 hull HP.
- Courier: 10 cannon rounds; no torpedo or point-defence shots; 0/1920 hull HP.
- Rescue party: deployed, unrecovered, zero completed work and no recorded party
  deaths. There is no replacement recovery ship in this run.
- Original delivery remains active. Only approach and distress response are
  completed, out of the eight original objectives.
- Raider remains hostile, with zero takeover progress and no capture of Morrow
  at the end of capture.

The weapon sequence is working, but this is a failed rescue, not evidence of
successful combat balance. The Courier's unfired torpedoes and point defence are
an outstanding combat-readiness finding.

## Decision boundaries and validation

The ship trees remain generic. Scenario posture and assigned opponent supply
attack authority; the bearing supplies direction without revealing position or
range. Steering uses a local lookahead along that ray. Ammunition choice uses
fitted guidance, availability and reach, with no resource-name checks. The
launcher uses homing rounds for unknown range, then the shortest-reaching
available round that reaches an observed contact. Loaded ammunition is checked
while switching. Weapon enable, target and magazine settings use ordinary
accepted Controller channels and respect human ownership.

An anonymous bearing alone still cannot authorise offensive fire. The native
combat suite verifies that boundary. A synthetic interdiction integration case
verifies homing release before positional acquisition, subsequent cannon and
short-round use, and exact save/resume during bearing pursuit. Native
interdiction, combat and Controller integration suites pass. Focused C++
formatting checks pass. No WASM or device-performance claim follows from this
manual run.

The first diagnostic run exposed the separate offensive-authority gate: its
launcher waited for positional contact before firing. The final run above
includes the correction connecting an explicit scenario attack order to homing
release against its assigned bearing. Ordinary anonymous contacts retain the
existing restriction.

## Reproduction

```powershell
.\build\native\Release\phoenix_rescue_ai_run.exe build/delivery-base.zip base.rescue-under-threat base.courier 4000 build/rescue-ai-long-range-final build/ai-raider-engagement.zip
```

Capture ended through the runner's `.stop` file after destruction and settlement;
this sends no simulation command. Ignored local artifacts are
`build/rescue-ai-long-range-final.{state.json,trace.jsonl,receipt.json}`. The receipt
records exact pack, executable, state and trace SHA-256 hashes, first weapon
samples, combat state, away state and the mission ledger. The earlier diagnostic
capture is `build/rescue-ai-raider-engagement.*`.
