# Rescue rerun with Veil disruption, 31 October 2026

Manual native headless smoke at code revision `f6dc21d3`:
fully AI Courier in `base.rescue-under-threat`. The full base pack and explicit
objective/policy overlay are byte-identical to the [previous run](autonomous-delivery-2026-10-31.md).
This rerun includes physical Veil suppression during weapon fire, docking and away
operations. No ship, scenario or AI policy tuning was changed for this run.

**8 of 8 original objectives completed.** Cairn received
100 supplies; Courier retained 0. The party was recovered and
Morrow escaped. Courier finished with 1920/1920 compartment hull HP.

## Journey

| Simulation time | Observed event                                                                              |
| --------------- | ------------------------------------------------------------------------------------------- |
| 0:11.80         | Completes station approach; newer distress navigation interrupts delivery.                  |
| 1:41.90–1:46.90 | Raider demands withdrawal; scenario Comms refuses and hostilities begin.                    |
| 2:32.90         | Physical docking permits deployment of the prepared rescue party.                           |
| 3:54.35         | Reactor secured first, using the delivered critical-reactor information.                    |
| 4:34.40         | All four trapped crew rescued and stabilised; no crew deaths.                               |
| 5:00.40         | Raider destroyed. Courier fired 223 rounds; raider fired 0.                                 |
| 6:07.00         | Rescue party recovered.                                                                     |
| 6:39.10         | Morrow departs safely.                                                                      |
| 6:42–6:48       | Original delivery navigation resumes and impulse activates on the return leg.               |
| By 8:09         | Courier docks at Cairn's west port; actual internal/crossing/receiving cargo work proceeds. |
| 9:08.70         | All 100 supplies reach Cairn Receiving; original delivery objective completes.              |

The original delivery objective retains its identity and tick-0 creation time.
There is no new return objective or scripted post-rescue order. Only one cargo
request was issued; final outstanding requests/batches are 0/0.
Courier had completed separation by the end of capture, at
9:17.00. It remains in-system, so no departure debrief was frozen.

## Raider finding

Veil suppression does not make the raider retaliate in this setup. The live sample
at 4:00.00 has Courier docked, its party deployed and a recent
weapon-emission receipt. These predicates suppress effective concealment under
the verified runtime rule. Nevertheless, the raider has no normal radar tracks.

Its inherited `base.equipment.combat-radar` authors a **175 m normal acquisition
range**. Its formation movement holds it roughly 390 m south of Morrow; the sampled
Courier–raider centre separation is 391.17 m. Long-range bearing coverage is a
separate capability and does not supply a normal tracked contact to the encounter's
attack rule. That rule requires a permitted player contact before selecting an
attack target or closing to combat stand-off. Without it, movement follows the
Morrow formation position. This leaves the raider outside its acquisition range.

The raider demands withdrawal and remains hostile, but fires **0 shots**, gains
**0 takeover progress**, and captures neither Morrow nor the visiting party.
The previous empty radar picture did not establish Veil as the cause of inactivity;
this run identifies the remaining acquisition/formation constraint. Enemy policy
and sensor tuning were left unchanged so the requested comparison remains clear.
This is an observed journey, not proof of demanding combat balance.

## Reproduction and artifacts

```powershell
.\build\native\Release\phoenix_rescue_ai_run.exe build/delivery-base.zip base.rescue-under-threat base.courier 24000 build/rescue-ai-veil-disruption build/ai-objective-delivery-final.zip
```

`build/rescue-ai-veil-disruption.receipt.json` records exact pack/executable/state/
trace SHA-256 hashes, objective identities, final physical quantities and ledger
events. The `.trace.jsonl` additionally samples dock connections, stocks, both
sides' command sections and radar-track histories. `-exposed.state.json` preserves
the live exposure sample; `.state.json` is the final state. Large outputs and the
diagnostic runner remain local ignored build artifacts.

Capture ended through a `.stop` file after all objectives completed and ordinary
settlement; it does not send a simulation command. No human claims were present.
Ledger times retain tick precision; sampled docking and travel times have
one-second resolution. The setup uses full normal artwork without portrait
fallbacks. No simulation-source or policy changes, or additional automated regression checks, were
required for this manually requested smoke; the exposure change's native psychic
and away integration checks were verified in the preceding implementation step.
No WASM, device/accessibility or six-screen performance claim follows from this run.
