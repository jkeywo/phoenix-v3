# Veil, rear approach and raider boarding, 31 October 2026

Manually triggered native headless Courier run in `base.rescue-under-threat`.
Combat gameplay revision `b35ff1e4`, frozen parent `build/delivery-base.zip` and
child `build/ai-contact-break.zip`. Authored AI only: no human claims, injected
ship orders or edited simulation states.

**The Courier breaks contact, reaches the raider's rear and destroys it while
the away mission continues.** The raider starts physical docking approaches to
Morrow when it cannot engage the Courier. It never completes docking or takeover.
Recovery and station delivery remain unfinished: Morrow's enabled FTL reserve
blocks the fresh docking connection needed to recover the detached ship's team.

## Why the previous run failed

Veil reduced ship sensor ranges but thrust, active scanning and PDC firing raised
emissions. The sideways waypoint also cut toward the pursuing raider. More
seriously, homing seekers received live target positions without concealment,
and the boarder's threat gate used true nearby player geometry even when hidden.

The fixes preserve physical sensing rather than making Veil invulnerability:

- Seekers scale acquisition range by visibility. Without acquisition, missiles
  retain their last heading and altitude solution; they can reacquire or collide.
- Active scan readiness switches off while funded Veil operates. Target intent
  remains available to weapons through the ordinary blackboard.
- PDC readiness uses permitted radar threats, not Veil intent. Current locks and
  projected collision courses can wake it; it stops when those threats disappear.
- Threat facts reach every Console with permitted radar/equipment feeds, including
  Tactical when another Console is the equipment's first observation owner.
- The Courier opens range before circling, then approaches the rear and holds
  offensive fire until both rear geometry and fitted weapon reach permit it.
- An unseen player cannot block boarding through true geometry. A bearing is an
  engagement opportunity only while homing ammunition is actually available.

All ship policies remain mission-independent. No replacement objectives or
post-interruption delivery orders were added. The scenario retains the target,
away work, decisions, arrival delay and takeover consequences.

## Observed mission

| Simulation time | Observation                                                                                                  |
| --------------- | ------------------------------------------------------------------------------------------------------------ |
| 2:32.90         | Rescue teams deploy aboard Morrow.                                                                           |
| By 2:35         | Courier finishes separation.                                                                                 |
| 2:41.80         | Raider arrives.                                                                                              |
| By 2:44         | Courier launches its opening homing missile.                                                                 |
| By 2:45         | Veil operates; active scanner readiness is off.                                                              |
| By 2:53         | Courier PDC begins intercepting radar threats.                                                               |
| 2:52–3:14       | Raider makes several approach attempts as contact is lost and regained.                                      |
| By 3:14         | Raider begins its sustained assisted approach to Morrow.                                                     |
| By 3:39         | Raider has spent its 16 homing missiles; Courier has stopped at 129 PDC rounds and repaired to full hull HP. |
| 3:54.35         | Teams secure the reactor.                                                                                    |
| By 4:49         | Courier is behind the raider, about 483 m away.                                                              |
| By 4:50         | Both Courier cannon turrets fire from the rear.                                                              |
| By 4:57         | Both torpedo tubes have participated in the rear attack.                                                     |
| 5:30.75         | Raider destruction is recorded.                                                                              |
| 5:55.90         | Away propulsion work finishes.                                                                               |
| 6:06.00         | Away operation completes its work and requests recovery.                                                     |
| 6:36.05         | Morrow's FTL reserve becomes ready.                                                                          |
| 10:45           | Combat capture ends with recovery still blocked.                                                             |

Geometry, weapon and docking samples have one-second resolution. Ledger events
retain tick precision. The raider's final approach ends by 5:19 as combat disables
it. No physical docking completion or takeover progress is recorded.

At the end of the combat capture:

- Courier: 1920/1920 hull HP; 16 homing missiles, 50 cannon rounds and 129 PDC
  rounds. Sixty-five additional offensive shots occur after reaching the rear.
- Raider: 0/3300 hull HP; 16 homing missiles and six PDC rounds. No cannons or
  Havocs: it does not regain the required positional firing solution.
- Teams: work complete, four trapped crew rescued, no recorded trapped or team
  deaths; still deployed and unrecovered.
- Five of eight original objectives complete. Recovery, casualty escape and
  station delivery remain active. All 100 supplies remain aboard Courier.

## Validation and limits

Native Psychic, Interdictions, Away, Combat and Controller runtime suites pass.
New synthetic cases prove loss of seeker guidance without hidden motion updates;
own-ship radar lock reporting; a disabled PDC waking on a different permitted
Console; and physical boarding against an unobserved player or a bearing that
cannot be engaged. Existing synthetic rear-fire/recovery and continuation cases
also pass. These do not freeze shipped ship balance.

A later recovery experiment did not resolve the FTL docking gate and was removed.
Its longer-distance fixture and runtime changes are not part of the verified
combat revision. The recovery diagnostic is not a completed mission claim.

The combat phase restores the unchanged 2:00 checkpoint. The captured resume-start
state equals that input exactly; no clocks, commands, resources or continuation
fields are edited. Initial setup and pre-combat history remain in the earlier
capture. Runtime fixes were rebuilt during diagnosis, so this is not a claim that
every diagnostic phase used one retained executable. No WebAssembly, graphical,
accessibility or six-screen smoke is claimed.

Local ignored evidence:

- `build/rescue-ai-contact-break-precombat.state.json`
- `build/rescue-ai-contact-break-verified.{resume-start.state.json,state.json,trace.jsonl}`
- `build/rescue-ai-contact-break.receipt.json`: parent, child and capture hashes;
  exact restore result.
- `build/contact-break-final-{psychic,interdictions,away}.log`
- `build/contact-break-verified-{combat,controller}.log`

The subsequent five-minute reactor tuning (`686e34b0`) is verified separately by
the production loader and one native tick using
`build/ai-contact-break-five-minute.zip`; see `build/reactor-five-minute-load.log`.
Its scenario threshold is 6000 ticks from system arrival, down from 7200.
Existing emergency consequences remain: controlled shutdown injures the actual
accompanying repair officer and requires 1600 work instead of the ordinary 800;
immediate shutdown kills that officer and requires 200 work. No accompanying
officer means no substituted officer/team casualty. The longer work still applies.

This capture
used the preceding six-minute clock; reactor security at 3:54.35 precedes both
thresholds and cancels the emergency. The five-minute variant has not received a
second complete playthrough.
