# Away missions and displacement

Away missions use compiled state trees and real ship personnel. Dispatch coordinates a manifest; each contributing Station keeps authority over its own officers, teams and transport. [Carried craft](carried-craft.md) supports individual shuttles and the crew's manually flown ship. Physical shuttles, disposable boarding torpedoes and target-authored security encounters also use these manifests and trees; see [Security and boarding](security-boarding.md). Evacuation cargo and a general investigation system remain later work.

## Playing the rescue

Choose **Rescue under threat** and the Courier or Cruiser. The casualty's delivered distress message reveals the away mission. Helm owns Dispatch; each contributing department retains authority over its personnel.

On a one-star Courier, Helm opens **Away missions** and chooses **Activate mission**. AI-operated team Controllers prepare eligible teams and their accompanying officers, including when Support is occupied. Teams physically stage at the ship's transport compartment. Helm docks at a compatible casualty port; the first transfer proceeds automatically once staging, capacity and docking permit it. The mission's blocker explains any wait. Cancel stops the activation. Recall remains deliberate, and a recovered party requires a new dispatch.

At manual team workloads, contribute through the team's own interface: open Repair, select a team and choose **Send on [mission]**. Support has no Away Missions tab. At a manual Personnel workload, an optional specialist slot appears against eligible officers in the roster. Automatic personnel workloads omit the Personnel tab. Personnel committed to a mission cannot be reassigned or used by another mission.

Other transports retain their ordinary Dispatch controls. Displacement waits for range, power, a qualified navigator and lowered obstructing shield sectors; shields are never lowered automatically. Shuttle deployment uses real carried craft. Transport bindings are shared, with one transfer at a time in acceptance order. Cancelling a transfer leaves personnel at their origin and does not refund energy already consumed.

The party reports and asks for priorities through Comms; see [Rescue priorities and consequences](rescue-away-mission.md). Repair work restores actual damage. Completing work does not recover the party: Dispatch must recall it through its available transport. Recovery preserves repairs and accumulated effort, allowing a later deployment to resume at the authored checkpoint.

## Authoring contract

- Fitted `displacement` Systems specify `range_mm`, `capacity`, `transfer_ticks`, `energy_per_second`, `power_store` and a personnel `compartment`. Capacity counts whole teams and individual officers, including attached officers. Fitted `dispatch` Systems provide mission-management authority. Both use ordinary Controller-owned `system_enabled` commands.
- A Navigator personnel duty has `required_trait`, referencing an authored trait. Its Controller must consume the displacement System. Assignment and starting-roster validation enforce the qualification; physical displacement additionally requires the officer to have arrived. Numeric expertise modifiers are unchanged.
- Maps author `away_missions`: `id`, `title_key`, `ship_slot`, `target_entity`, `conversation`, `reveal_event`, `tree` and `slots`. For this version, `reveal_event` names the **text key of a delivered introduction line** in the bound conversation, rather than a hidden scenario event. Each slot has `id`, `kind` (`team` or `officer`), `required`, and an optional officer `trait`. Team slots accept generic teams and may filter `team_type` or `capability`. Medical teams provide optional real patient care; repair work counts repair-capable teams only. See [Teams and Medical](medical-teams.md).
- Trees use `context: "away"`. Observations are `deployed`, `recovered`, `work_done`, `tick`, `progress`, `answer` and `checkpoint`. Actions are `work(integer work units)`, `checkpoint(text)`, `report(conversation event)`, `emit(scenario event)`, `restore_compartment(target compartment)`, `complete(reason)` and `fail(reason)`. Arguments are compiled literals. `restore_compartment` starts structural repair by a deployed party; place it after the checkpoint and before `work`. `work_done` requires both the authored work budget and full restoration.
- Work advances only while deployed. Teams use the ordinary authored repair rate, fault effectiveness and stacking penalty; attached and separately assigned officers contribute through the personnel modifier formula. A work unit is one HP-equivalent of effort; standalone officers perform generic work but cannot repair structure without a team. Structural HP and recovery advance through the same fractional-work mechanism as onboard repair, and recovery pauses that work without undoing it. Re-entering the same checkpoint and work amount preserves effort. Trees explicitly route recovery and redeployment; ordinary tree timing and Comms deadlines continue during link loss. Completed effects are journalled rather than replayed on load.
- Away conversation prompts require a timeout and a default. The mission tree reads the resolved answer, while the existing Comms runtime owns delivery, choices and history. Dispatch projections omit remote progress and outcomes; reports must arrive through Comms.

Workshop exposes mission definitions, slots, displacement fittings, qualification requirements and the away tree vocabulary through its existing editing and verification flow.

## Commands, persistence and verification

Human commands use `away.open`, `away.assign`, `away.withdraw`, `away.transport`, `away.send`, `away.recall` and `away.cancel`. They carry the mission ID in `system`, observed manifest revision in `value`, Controller identity/generation, and operation text. Assignment text is `slot/asset`, with assets `team:<id>` or `officer:<id>`. Transport text is the fitted System ID. AI trees expose the same actions with `(mission, text, revision)` arguments and use the same admission path.

Current away records use per-mission schema 11 inside World/portable-session schema 32. The reader accepts only the current mission schema; older records are rejected instead of reconstructed or restarted. Saves preserve assignments, accompanying officers, physical transport, patient and repair progress, retained team activities and tree continuations. The active structural-repair target is saved alongside the checkpoint. Content identity checks still apply, and mission-bearing content requires matching mission records. Deployment is derived from the typed phase; a redundant saved `deployed` flag is rejected.

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

## Shared effect groups

Away trees can invoke literal shared effect groups with `apply_effect_group [group, self|target]`; see the [final coverage and authoring contract](effect-coverage-audit.md#direct-native-tree-effects). Native mission work, deployment, transport and outcome reporting remain unchanged. The current enclosing World and portable-session schema is 32; older historical schema notes above describe earlier increments.

## Assisted docking activation

An occupied Console can contain AI-operated team Controllers. These Controllers authorise their own department contribution to an opened mission through ordinary `away.contribute` admission; Console occupancy does not disable team preparation. Human-operated team Controllers retain their contribution authority.

For one-star Dispatch, activating a direct-docking mission selects ship transport and prepares available teams and accompanying officers. Its first insertion uses ordinary `away.send` admission once staging and physical docking permit it. Until then, the mission remains open and its normal blocker explains the wait. Cancellation returns it to available; recovered parties require a deliberate new dispatch. Shuttle launches and supply collection retain their existing explicit dispatch controls. The derived `automatic_dispatch` projection carries the same behaviour and activation label to native and remote browser Consoles; it grants no additional command authority.

Verification on 27 October 2026: native and WASM away integration passed the occupied-Console AI team preparation, accompanying officer, physical docking insertion, cancellation, recall, no automatic redeployment, remote projection and save/resume checks. Existing manual ownership and transport cases passed in the same suite; native/WASM away continuation traces matched (`54d8ded4995500c72c90672a2295e673cc201f2f1021bbc71207e82c2d79fda4`). Native security/boarding integration, the native application build/check and browser server/client builds passed. This increment did not include a fresh graphical or physical-device walkthrough. Evidence is in `build/assisted-away-*.log`; graphical acceptance remains separate.
