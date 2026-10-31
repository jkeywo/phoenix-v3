# Autonomous rescue smoke, 31 October 2026

Manual native headless smoke: `base.rescue-under-threat`, `base.courier`, 12,000
50 ms ticks (ten simulated minutes), authored AI throughout, no human claims.
This records an observed journey, not a balance or performance acceptance test.

The generic policies prepare combat readiness, manage impulse travel and braking,
accept an offered away job, stage personnel, dock, deploy, recall and separate.
Scenario objectives, away trees and conversation trees retain the named choices.
Coordination uses permitted blackboards/accepted same-Console commands or
addressed messages; Controllers do not inspect each other's private continuations.

| Simulation time | Observed action and reason                                                                                                     |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| 0:01 onward     | Requests impulse for long, aligned travel; releases it for braking and approach. Peak sampled speed was 207.452 m/s.           |
| 0:12            | Completes station approach, then follows the distress objective.                                                               |
| 0:14            | Scenario reveals the repair offer; Dispatch opens it and stages Repair, Medical, Security and a specialist.                    |
| 1:42            | Pirate demand arrives; observed hostility raises fitted combat alert and reactor readiness.                                    |
| 1:47            | Scenario Comms chooses refusal, starting hostilities. Weapons select the hostile independently of the friendly scanner target. |
| 2:33            | Physical docking permits the away party to deploy aboard Morrow.                                                               |
| 2:43            | Scenario chooses reactor first using received `reactor_critical` information.                                                  |
| 3:54            | Reactor secured before the six-minute emergency.                                                                               |
| 4:04–4:34       | Scenario prioritises wounded; all four trapped crew are rescued and stabilised, with no deaths.                                |
| 5:00            | Pirate destroyed. Courier fired 223 gun rounds across its two turrets.                                                         |
| 5:56            | Propulsion work finishes and steering is restored.                                                                             |
| 6:06–6:07       | Scenario returns the vessel to its crew; away party recovered.                                                                 |
| 6:39            | Morrow departs safely. Courier remains in system through ten minutes, with 1920/1920 compartment hull HP.                      |

Seven rescue/navigation objectives completed. The station delivery objective
remains active and all 100 supply units remain aboard Courier. This run therefore
demonstrates the rescue and combat sequence, not completion of every mission
objective. The recovered specialist is healthy and off duty (`present: false` is
duty attendance, not evidence of being left aboard Morrow); postings are cleared.

## Reproduction and artifacts

```powershell
.\build\native\Release\phoenix_rescue_ai_run.exe build/ai-rescue-base.dat base.rescue-under-threat base.courier 12000 build/rescue-ai-operations build/ai-operations-smoke.zip
```

The diagnostic executable and large artifacts are local ignored build outputs.
`build/rescue-ai-operations.receipt.json` records artifact sizes, SHA-256 hashes,
the command and event times. The `.trace.jsonl` contains sampled observations;
the `.state.json` and `.checkpoint.json` retain full state. The runner's sampled
`docking` rows are physical port settings, not the full `dock_connection`.

Content used a frozen, packager-verified `build/ai-rescue-base.dat` with portrait
fallbacks for missing source artwork, plus explicit AI policy overrides in
`build/ai-operations-smoke.zip`. This isolates concurrent artwork changes. It does
not verify the current loose production artwork pack, whose portrait preparation
and archive-size issues remain outside this change.

Synthetic native integration checks cover offered/unoffered jobs, human operation
ownership, real docking and deployment/recovery, impulse during assisted approach,
saved continuation, independent hostile targeting and shared-command fallback.
The native away, Controller and docking suites pass. Gameplay-flight reaches a
notification assertion and fails: expected `Helm: Notice: Supply 7`, observed
`Request sent: Send test.notice`; running from the CTest working directory gives
the same failure. No notification code or fixture was changed here. WASM and
hardware/accessibility acceptance are not established here.
