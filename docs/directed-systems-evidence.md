# Directed systems and payload migration evidence

24 October 2026. Implementation follows the [migration plan](directed-systems-migration-plan.md), using the delivered resource/store interfaces at `99726b1`. The isolated branch `codex/directed-payloads` includes the subsequently committed GUI navigation work at `68e2a7c`, preserving unrelated uncommitted work in the primary checkout. Implementation is committed at `df2e5c0`. Automatic integration validation and independent review passed; manual acceptance remains separate.

## Execution and ownership

Weapons and focused scanners compile reusable operating modes and targeting policies from the effects catalogue. Each capability owns independent selections, target intent, readiness and an execution sequence. Native mechanisms retain aiming, loading, cooldowns, beam fractions, collision and sensor evidence. The [authoring reference](directed-systems-content.md) describes the compiled definitions and their bounds.

The selected mode runs through shared qualification and funded dispatch. Launch preparation captures permitted aim, actual ammunition and payloads and claims required stock before processors prepare. Allocation removes unaffordable indivisible launches and requotes from the unchanged resource network. Rejected claims are released; blocked processors retry their original work budgets without advancing fractional work or consuming ingredients twice. Only the final allocation commits. Sustained beams retain proportional funding; focused scans require full funding. Native broad radar remains separately funded and does not acquire facts from inactive scan modes.

Switching operating mode preserves loaded-resource return commitments, original loading mode and quantity, cooldowns and per-mode acquisition progress. Targeting changes remain separate: assigning a target does not implicitly switch point defence between Off, Auto and Manual. Simultaneous capabilities use separate System identities and independently owned mounts.

Native damage resolution emits explicit reached shield-contribution, compartment, System and entity outcomes. Payloads use the actual collision target. Resource-attached payloads are captured only by launch operations that consume ammunition. Impact, ordnance destruction and ship destruction carry distinct authored payloads through the existing physical blast rules.

Periodic conditions capture their target context and survive source loss. Same-source applications refresh expiry while preserving cadence; distinct sources remain independent. Expiry is exclusive, and the first tick follows a full period. At the deferred-effect phase, expired or unavailable conditions are removed, due periodic instances run in stable order, then due hit bindings run in stable order. Newly generated hit work waits until the next tick. Secondary failure records its own result without undoing the native hit. Explicit removal supports one grant or all sources of a named periodic condition. Fire spreading and firefighting gameplay remain outside this migration.

## Content, controls and continuation

Maintained base and development packs use shared mode and targeting references. Workshop exposes directed modes, payload bindings and periodic definitions, and export retains their dependency closure. Generated Controller trees are regenerated from authored control layouts when Workshop reloads a flattened draft.

Production verification identified shared legacy mounts on the armed courier, pirate raider, rescue courier and rescue casualty. Their beam and point-defence weapons now have separate turret identities with copied geometry, traverse and faults; existing control, feed and officer-modifier bindings include the new mounts. The main Cruiser already had independently owned mounts. No weapon costs, ranges or ammunition balance were changed.

Human and AI commands share admitted directed target/mode settings. Console projections expose only permitted equipment and mode choices, without authoritative payload definitions or hidden target details. Network projection validates the new board. Existing weapon/sensor command aliases address the same settings, with physical-input conflict validation preventing duplicate writers.

World and portable session schema **24** persist directed selections, loading identities, per-mode scan progress, captured projectile payloads, deferred jobs, periodic instances and execution identities. Replacement restore validates the compiled roster, bindings, lifetime bounds and native commitments before replacing the live world. Earlier runtime schemas require a new session.

## Independent review

The reviewer identified inactive scan capability leakage, broad radar funding overwritten by mode costs, beam reuse of ammunition payloads, unreachable mode/policy combinations and non-atomic launch funding. Fixes separate passive acquisitions, preserve native radar baseline demand, restrict resource payload capture to launches, reject disconnected compatibility graphs and stage launch inputs before funding. Follow-up review identified processor starvation from rejected launch claims; same-tick release and budget-preserving retry resolve it.

Integration review also caught generated Controller layouts emitting both legacy aliases and canonical inputs, followed by an unusable legacy mode input on ordinary weapons. Generated layouts now emit canonical directed commands and preserve authored initial selections; explicit authored aliases still work under the single-writer rule. A regression exercises target assignment while off, designated fire and stopping fire. WebAssembly compilation exposed a standard-library header crossing a named-module boundary; the bounded projection validator now uses already available containers.

The reviewer also flagged the old implicit point-defence targeting behavior. Separate target/policy selection is intentional under the agreed design; command journeys are migrated explicitly. The final static re-review found no remaining defects in the corrected paths. Test execution is recorded separately below.

## Validation

- `scripts/check.ps1 -Target native` passed: complete native build, all **41** CTest integration cases, native application build check, all **19** continuation traces and all **12** live-network cases. All **29** HTML launcher tests passed.
- Base and test-missions packs verified. The separate `development_packs` target verified test and test-3d packs. Verification checks definitions without freezing shipped balance.
- After the WebAssembly module-header fix, a final complete native rebuild, eight affected regression suites and the application build check passed. The base pack was regenerated from the final formatted definitions.
- Independent review has no remaining concrete findings. Final C++ formatting and whitespace checks passed.
- The complete WebAssembly build and `scripts/check.ps1 -Target web` passed: production-content preflight, all integration suites and all **19** native/WebAssembly continuation comparisons, including directed operations, periodic payloads, processors and shared stock effects.

Logs are retained in the isolated worktree under `build/native-gate.log`, `build/native-final-build.log`, `build/native-final-check.log`, `build/development-packs.log`, `build/web-build-2.log` and `build/web-gate.log`.

No graphical, touch, gamepad, screen-reader, six-display performance or browser-storage smoke journey has been run for this migration. Linux checks were updated but not executed on Linux.
