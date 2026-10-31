# Autonomous rescue and delivery, 31 October 2026

Manual native headless run: `base.rescue-under-threat`, fully AI `base.courier`.
The capture ended at tick 11,100 (9:15 simulated) after all eight original
objectives completed and ordinary undocking finished. No human claims, injected
ship commands or model-provider decisions were used.

## What changed

The original delivery condition objective now discloses its charted destination
and consignment. Generic navigation selects an unfinished navigable objective,
resumes it after interruption, and follows its location without requiring a radar
contact. Generic Helm hands over to docking after observing a compatible target;
logistics submits one finite consignment and retains its receipt through interruption.
Controllers coordinate through permitted blackboards and ordinary messages.

There are no new return objectives, new rescue scenario states or post-rescue
orders. The eight original objective definitions are retained. Station, port,
receiving store, material, quantity and hull-specific material selection are data
on the original scenario objective. Rescue decisions remain in its away and
conversation programs.

## Observed journey

| Simulation time | Observed action and policy basis                                                                                                                                                                 |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0:12            | Completes the arrival approach and follows the newer distress objective.                                                                                                                         |
| 0:14            | The revealed rescue offer is opened and teams are staged.                                                                                                                                        |
| 1:42–1:47       | Raider demands withdrawal; Comms refuses. Observed hostility raises readiness and weapons select the hostile.                                                                                    |
| 2:33            | Courier docks with Morrow and deploys the party.                                                                                                                                                 |
| 2:43–3:54       | Received reactor-critical information selects reactor work first; containment is secured before its emergency.                                                                                   |
| 4:34            | All four trapped crew are rescued and stabilised, with no deaths.                                                                                                                                |
| 5:00            | Raider destroyed after Courier fires 223 gun rounds.                                                                                                                                             |
| 5:56–6:07       | Propulsion work finishes, the ship is returned to its crew, and the party is recovered.                                                                                                          |
| 6:39            | Morrow departs safely.                                                                                                                                                                           |
| 6:42–6:48       | With the close obstruction clear, Courier follows the original delivery destination; return impulse becomes active. The original delivery objective remains the same instance created at tick 0. |
| By 8:09         | Observed station contact enables assisted docking at Cairn's west port. Physical cargo handling follows.                                                                                         |
| 9:08.70         | Cairn Receiving contains all 100 supply units; the original delivery condition completes.                                                                                                        |
| By 9:11         | Ordinary separation finishes. Capture ends at 9:15.                                                                                                                                              |

Final physical state: Cairn has 100 station-supply units, Courier has zero, and no
cargo requests or batches remain. There was one transfer request. All eight
objectives are completed; Courier retains 1920/1920 compartment hull HP. The rescue
party is recovered. Courier remains in-system, so no departure debrief was frozen.

## Raider behaviour

The single raider demands withdrawal, adopts hostile posture and advances toward
Morrow. Its takeover is blocked and never accumulates progress; it neither captures
the vessel nor its visitors. Courier's Veil remains active. A saved live sample at
3:00 has no tracks in the raider's radar and no valid Courier weapon target. The
encounter's attack rule requires a permitted contact, so hostility alone does not
supply a firing solution. The raider holds roughly 390 m south of Morrow and is
destroyed at 5:00.40 without firing any recorded gun, beam, missile or PDC shot.
This run demonstrates that outcome; it does not establish a demanding combat balance.

## Reproduction and verification

```powershell
.\build\native\Release\phoenix_rescue_ai_run.exe build/delivery-base.zip base.rescue-under-threat base.courier 16000 build/rescue-ai-objective-delivery build/ai-objective-delivery-final.zip
```

`build/delivery-base.zip` is a frozen current full native base pack; the child pack
explicitly overrides only the original objective and reusable policies. It uses
normal artwork, with no portrait fallbacks. Both load through production pack
resolution and content verification. The diagnostic runner is local ignored build
output. Its `.stop` file ends capture after success without changing simulation
state; the maximum invocation budget was 16,000 ticks.

`build/rescue-ai-objective-delivery.receipt.json` records exact artifact SHA-256
hashes, setup, tick count, objective identities, physical totals and ledger events.
The `.trace.jsonl`, `.state.json`, `.checkpoint.json` and `-raider.state.json` retain
local diagnostic evidence. Sampled docking transition times have one-second
resolution; ledger completion times retain tick precision. Large artifacts remain
outside source control.

Native docking, Controller, away and mission suites pass. Independent synthetic
fixtures cover resuming an older objective after a terminal diversion, explicit
priority and scope, human logistics authority, actual docking/cargo conservation,
malformed consignment data, duplicate prevention and deterministic save/resume.
This run does not establish WASM, device/accessibility or six-screen performance
acceptance. The previously reported flight notification assertion was not rerun.
