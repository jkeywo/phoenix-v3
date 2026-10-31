# Rescue deployment during combat, 31 October 2026

Manually triggered native headless run with a fully AI Courier in
`base.rescue-under-threat`. Gameplay changes through `e63a3cda`; no human claims,
manual ship commands, replacement objectives or edited simulation states.

**The requested overlap now happens:** Courier docks, deploys the teams and
separates before the raider arrives. The team secures Morrow's reactor during
combat. **The Courier is subsequently destroyed.** Sustained rear-arc fire,
complete rescue, recovery and resumed station delivery were not achieved.

## Observed timeline

| Simulation time | Observation                                                     |
| --------------- | --------------------------------------------------------------- |
| 0:11.80         | Distress interrupts the original delivery.                      |
| 1:34.90         | Distress response objective completes.                          |
| By 2:30         | Courier begins physical mating with Morrow.                     |
| By 2:32         | Docked.                                                         |
| 2:32.90         | Teams deploy.                                                   |
| By 2:34–2:35    | Separation begins, then Courier is free.                        |
| 2:41.80         | Delayed raider arrival.                                         |
| 2:41.90         | Raider demands withdrawal.                                      |
| 2:42.90         | Away policy chooses to secure the reactor.                      |
| By 2:44         | Courier fires one homing missile.                               |
| 2:44.00         | Scenario records pirate hostilities.                            |
| By 2:45         | Raider's first homing missile.                                  |
| By 2:50         | Courier PDC begins firing.                                      |
| By 3:00         | Raider cannons join the engagement.                             |
| 3:54.35         | Reactor work completes; `restore-services` objective completes. |
| 3:57.15         | Mission ledger records Courier destruction.                     |
| 4:19            | Capture ends after the loss.                                    |

Docking, weapon and geometry samples have one-second resolution. Ledger events
retain tick precision. Arrival comes from the interdiction's authored delay.

The Courier launches before its flanking fire hold applies; its Veil contribution
is active by 2:45. It manoeuvres under
ordinary flight guidance and uses PDC against incoming missiles. Two physical
missile-destruction bursts are recorded at 2:57.55 and 3:04.40, with preceding
nearby samples identifying raider-sourced incoming homing missiles. This sampled
trace is not an exact per-round kill attribution. The raider also uses its PDC.

The raider turns to keep Courier ahead. Courier does not reach a sustained rear
firing position and fires no cannon rounds. Veil remains subject to funding,
strain and physical emissions; requested concealment is not immunity to tracking.
The shared missile defence improvements apply to both ships.

Final physical counts:

- Courier: one homing missile, 71 PDC rounds, no cannon fire; 0/1920 hull HP.
- Raider: 16 homing missiles, 27 Havocs, 25 cannon rounds and six PDC rounds;
  3300/3300 hull HP. Missile identities confirm all 16 homing launches.
- Three of the eight original objectives complete: approach, distress response
  and reactor services. Remaining rescue and delivery objectives remain active.
- Teams remain deployed, with the reactor checkpoint saved and no recorded team
  deaths. They are not recovered. All 100 station supplies remain in Courier's
  cargo hold; no station delivery is claimed.
- Raider remains hostile; no takeover progress or Morrow capture is recorded.

## Changes and validation

The scenario interdiction delay increases from 1800 to 3000 ticks: 90 to 150
seconds after the distress trigger. Its away offer requests `combat_cover`.
Mission-specific work, choices and consequences remain in scenario/away trees;
ship policies contain no scenario IDs and receive no additional objectives.

Generic policies coordinate the opening launch, deployment separation, shared
propulsion/steering destination, rear approach and return for recovery. Coordination
uses permitted blackboard observations and existing messages, not another AI's
private state. Offensive fire pauses during flanking and recovery; PDC retains
its interception priority. Ship-transport recall awaiting a dock does not alone
disrupt Veil. Actual docking, transfer and firing still expose the ship.

PDC pre-aim and ballistic interception corrections, plus the shared ammunition
deposition/range corrections, address earlier failed interception attempts.
The preceding runs exposed real failures: premature Veil strain, conflicting
propulsion/steering destinations and stopping at a side waypoint. They are
diagnostic evidence, not successful playthroughs.

Native Away, Controller runtime and Combat integration suites pass. The synthetic
Away case independently requires two flight policies to reach a passive hostile's
rear, fire, return to dock and recover the team, including deterministic docking
continuation. That protects the manoeuvre contract without freezing shipped ship
balance. It does not establish success against this actively turning raider.
No browser, six-screen, accessibility or WebAssembly smoke is claimed.

## Local artifacts

Frozen full parent `build/delivery-base.zip` plus child
`build/ai-cover-delayed-arrival.zip` isolate the authored gameplay changes from
unrelated Workshop/content drafts. They use the production loader and resolver.

The first phase runs to tick 3120 (2:36); the second restores that complete state
with the same executable and packs. Parsed saved state equals the captured
resume-start state exactly. No time, seed, command or continuation fields change.

- `build/rescue-ai-cover-launch-clear.{state.json,trace.jsonl}`
- `build/rescue-ai-cover-final.{resume-start.state.json,state.json,trace.jsonl,json}`
- `build/rescue-ai-cover-final.receipt.json`: file sizes, SHA-256 hashes and exact
  restore result.
- `build/cover-route-away.log`, `build/cover-controller-final.log`,
  `build/cover-combat-final.log`: passing native integration results.

These ignored captures are local evidence, not committed fixtures or balance
goldens. The receipt identifies the exact parent, child and executable bytes.
