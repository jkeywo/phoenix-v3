# Away missions and displacement

Away missions use compiled state trees and real ship personnel. Dispatch coordinates a manifest; each contributing Station keeps authority over its own officers, repair teams and transport. This increment implements displacement and repair work, not shuttles, security encounters, evacuation cargo or a general investigation system.

## Playing the rescue

Choose **Rescue under threat** and the **Courier — Rescue watch**. Helm owns Dispatch and the Navigation department; Engineering is the Support Console and owns the repair teams. The Guild navigator begins assigned to the displacement Controller and stays aboard.

The casualty's delivered distress message reveals the mission. Helm opens **Away missions**, opens assignments and assigns displacement. Engineering's Away missions button highlights and says assignment is needed. Engineering assigns a repair team; its attached officer is included. An optional specialist slot accepts an officer with the repair trait. Personnel committed to the mission cannot be reassigned or used by another mission. Team assignments use their owning Console Controller; return from Comms to Engineering to contribute another repair team.

Teams walk to the displacement compartment before Send becomes executable. Dispatch sends the manifest; the transfer waits for range, power, a qualified navigator and lowered obstructing shield sectors. Shields are never lowered automatically. Transport bindings are shared, with one transfer at a time in acceptance order. Cancelling a transfer leaves the personnel at their origin and does not refund energy already consumed.

The away party reports through Comms and asks whether to stabilise services or prioritise propulsion. A timed default stabilises services. Repair work restores actual damaged compartments. An authored pirate arrival creates a bounded threat using the existing combat machinery. Completing repairs does not recover the team: Dispatch must recall it within displacement range. Recovery releases the manifest, preserves repairs and accumulated effort, and allows a new team to resume at the authored checkpoint.

## Authoring contract

- Fitted `displacement` Systems specify `range_mm`, `capacity`, `transfer_ticks`, `energy_per_second`, `power_store` and a personnel `compartment`. Capacity counts whole teams and individual officers, including attached officers. Fitted `dispatch` Systems provide mission-management authority. Both use ordinary Controller-owned `system_enabled` commands.
- A Navigator personnel duty has `required_trait`, referencing an authored trait. Its Controller must consume the displacement System. Assignment and starting-roster validation enforce the qualification; physical displacement additionally requires the officer to have arrived. Numeric expertise modifiers are unchanged.
- Maps author `away_missions`: `id`, `title_key`, `ship_slot`, `target_entity`, `conversation`, `reveal_event`, `tree` and `slots`. For this version, `reveal_event` names the **text key of a delivered introduction line** in the bound conversation, rather than a hidden scenario event. Each slot has `id`, `kind` (`team` or `officer`), `required`, and an optional officer `trait`. Team slots currently accept repair teams.
- Trees use `context: "away"`. Observations are `deployed`, `recovered`, `work_done`, `tick`, `progress`, `answer` and `checkpoint`. Actions are `work(integer work units)`, `checkpoint(text)`, `report(conversation event)`, `emit(scenario event)`, `restore_compartment(target compartment)`, `complete(reason)` and `fail(reason)`. Arguments are compiled literals. `restore_compartment` starts structural repair by a deployed party; place it after the checkpoint and before `work`. `work_done` requires both the authored work budget and full restoration.
- Work advances only while deployed. Teams use the ordinary authored repair rate, fault effectiveness and stacking penalty; attached and separately assigned officers contribute through the personnel modifier formula. A work unit is one HP-equivalent of effort; standalone officers perform generic work but cannot repair structure without a team. Structural HP and recovery advance through the same fractional-work mechanism as onboard repair, and recovery pauses that work without undoing it. Re-entering the same checkpoint and work amount preserves effort. Trees explicitly route recovery and redeployment; ordinary tree timing and Comms deadlines continue during link loss. Completed effects are journalled rather than replayed on load.
- Away conversation prompts require a timeout and a default. The mission tree reads the resolved answer, while the existing Comms runtime owns delivery, choices and history. Dispatch projections omit remote progress and outcomes; reports must arrive through Comms.

Workshop exposes mission definitions, slots, displacement fittings, qualification requirements and the away tree vocabulary through its existing editing and verification flow.

## Commands, persistence and verification

Human commands use `away.open`, `away.assign`, `away.withdraw`, `away.transport`, `away.send`, `away.recall` and `away.cancel`. They carry the mission ID in `system`, observed manifest revision in `value`, Controller identity/generation, and operation text. Assignment text is `slot/asset`, with assets `team:<id>` or `officer:<id>`. Transport text is the fitted System ID. AI trees expose the same actions with `(mission, text, revision)` arguments and use the same admission path.

Per-mission save schema 2 derives deployment from a typed phase and preserves assignments, accompanying officers, transfer order/progress, physical deployment, work, checkpoints, effects and tree continuation. Valid schema 1 mission records migrate on load; their redundant deployment flag must agree with the phase. The active structural-repair target is saved alongside the checkpoint. The enclosing World format is schema 13 following shared Controller actions. Older runtime schemas are rejected; valid content without missions remains supported, and saves for mission-bearing content must contain matching mission records. Content identity checks still apply.

Synthetic integration covers department and Controller ownership, required slots, qualification, staging, attached officers, transport queuing, range, stale commands, Comms defaults/link loss, recovery, save continuation, malformed state and network projection. Native and WASM run the same test source through the production loader and compare the resulting away continuation. Shipped content is verified without freezing its balance values.

The graphical journey is manually invoked:

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_away_smoke
./build/native/Release/phoenix_away_smoke.exe build/native
```

It renders Helm and Engineering separately, assigns transport/team through the UI, lowers shields through normal Controller commands, sends the party, answers through Comms, recalls it, then reassigns the team to resume and complete repairs. Actual browser UI, screen-reader, physical input and six-display performance acceptance remain separate.

Sera Ilyan has a dedicated [Navigator portrait](../content/base/assets/personnel/navigator.png), used through the ordinary officer portrait path.

Verification on 21 October 2026: all 26 native system cases passed; the complete WASM gate passed, including identical native/WASM away continuation; all eight network service cases passed. Two timed service cases failed during concurrent compilation and passed when rerun after the build. The manually invoked native rescue journey passed assignment, deployment, Comms choice, recall, redeployment, completion and final recovery. This is smoke evidence, not an automatic assertion of shipped tuning or physical-device acceptance.

Cleanup verification on 21 October 2026, based on away mission commit `4677d5d`: native and WASM away, combat, personnel and Controller integration cases passed. Native/WASM away and combat continuations matched. The production base pack verified all 19 entities, six maps and 12 consoles. Focused regressions cover disabled-team staging, rate-dependent partial remote restoration, recovery and repair continuation, bounded effect receipts, typed phases and schema 1 migration. Changed C++ and content/document formatting passed. Graphical/device smoke journeys were not rerun for this cleanup.
