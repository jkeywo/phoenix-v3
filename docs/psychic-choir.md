# Psychic choir

The Cruiser's Comms Station owns a fitted psychic choir, including protection. Science retains conventional scans and shields. House-trained psychic specialists are separate from Guild navigators.

Each healthy, present officer supplies one authored power. Several officers can serve a choir concurrently. Choir seats are personnel duties, with one officer per seat; assignment, travel, department ownership, injury and treatment use the existing Personnel and Medical rules. Removing an officer clears that seat's execution and planning intent. Other duties remain functional without named officers; psychic capability requires its specialist.

## Powers

**Cloak** reduces the projecting ship's detection distance. Its effective concealment is its funded strength multiplied by the remaining low-emission fraction. Actual generation, propulsion, active investigation, transmitted messages and recent weapon fire contribute to exposure. Weapon exposure persists for one simulation second. Firing does not switch cloak intent off. Existing observations remain dated, and lost real contacts retain predictions from their last observed motion with growing uncertainty.

**Scrambling** projects an authored group of apparent ships against a selected known victim. Apparatus range and a directional footprint aimed at the last permitted target position determine reception. Other receivers in that footprint can be affected, including allies. Each apparent ship follows authored speed, acceleration and turning constraints. A receiver also needs actual sensor coverage of its apparent position. Ghosts do not alter real ships' positions or details, nor reduce sensor range.

Ghosts enter the same permitted contact picture used by players and AI. They can be inspected, scanned and targeted. Weapons spend their normal ammunition and energy, and their trajectories continue through the apparent hull. A detailed scan or damaging weapon intersection dispels a ghost for that receiving ship. It fades instead of exploding. A rejection receipt prevents its immediate recreation within the same activation. Ward suppression and lost reception preserve evidence without establishing that a contact was false.

**Ward** protects one ship, self or a known ally, while apparatus coverage and funded work remain available. It suppresses ghost reception when its effective strength meets the incoming strength. It does not identify the attacker or erase old observations. Overlapping wards or scramblers use the strongest applicable contribution, with stable ordering for ties.

Apparatus failure, partial funding, structural faults and officer availability affect actual work. The choir requests the sum of its active powers' energy demands; its funded fraction applies proportionally to all seats. Thermal behavior comes from its real resource store and cooling links. No extra mana, fatigue or random psychic casualty system is introduced.

## Authoring and interfaces

An entity's `psychic_powers` object contains named profiles. Each has `kind` (`cloak`, `scramble`, `ward`), localised `label`, `strength_permille`, `energy_per_second` and `recovery_ticks`. Scrambling additionally supports `ghost_count` (1–16), `apparent_class`, `offset_mm`, `spacing_mm`, `speed_mm_s`, `acceleration_mm_s2`, `turn_mdeg_s`, `radius_mm`, `scan_ticks` and `fade_ticks`. Profiles can be reused through normal entity inheritance; fitted apparatus can be reused through equipment templates.

A `kind: "psychic"` System declares `compartment`, `power_store`, `range_mm`, `arc_mdeg`, compatible profile IDs in `powers`, and `seats`. Each seat binds a `duty`, `console`, `controller`, `enabled` channel and `target` channel. The channels are zero-initialised `intent_boolean` and bounded nonnegative `intent_integer` settings. The explicit seat binding makes these channels inputs to the psychic mechanism. There is one writer per channel, and the same Controller command executor admits human and AI changes. The apparatus itself uses an ordinary `system_enabled` channel.

A personnel duty binds `choir` instead of `controller` or `team`. An officer's optional `power` references a profile; starting choir assignments must be compatible. The apparatus and its seat duties occupy the same compartment. The owning department and Console must belong to the same Station. Every Controller still has its ordinary separate duty.

An optional `planning_controller` names a same-Console Controller with matching seat channels. Authored ordinary Controller trees can read these preferences and write execution intent. The Cruiser's Simple and Middle profiles expose planning while delegating execution; Detailed exposes execution directly. Automatic preserves ordinary authored intent. Target commands require known physical ships; ward targets must be self or observed allies.

Workshop exposes profiles, apparatus, seats and officer powers through its normal draft/Undo/Verify/Test flow. Missing powers, invalid geometry, foreign ownership and duplicate channel bindings fail verification.

Apparent contacts receive stable identities from the ordinary entity identity allocator without creating physical bodies. Their visible IDs do not classify them as ghosts. Weapon and scanner targets resolve apparent evidence without requiring a physical entity. Ghost provenance stays authoritative; permitted Console observations carry apparent evidence and only reveal a dispel after it happens. Network projections explicitly allowlist choir status, fading contacts and retained tracks. Console readouts and actions use the same model for graphical and semantic interaction.

## Persistence and verification

World saves optionally contain `psychic`: apparatus seat continuation, projection trajectories, observer contact identities, scan and rejection receipts, recovery deadlines and weapon emission deadlines. Funded strength is rebuilt from restored grants without advancing time or paying again. Sensor motion fields default to zero for older saves. Content without psychic fittings retains its previous capabilities.

`system.psychic` uses independent synthetic fittings through production loaders, command admission and authoritative execution. It covers concurrent powers, shortages, injury, reassignment and queued orders, sensor isolation, friendly reception, duplicate powers, wards, scan and weapon dispels, fading, hostile save input, network projection filtering and exact continuation. Cross-platform traces exclude fixture ZIP timestamps; within-run restore comparisons retain full content identities.

Verification on 22 October 2026:

- Native: all 33 runnable CTest cases passed, including `system.psychic`. The separate inference adapter case requires a local HTTP socket and was blocked by the sandbox.
- WASM: all 30 Node cases passed. All 11 native/WASM continuation comparisons matched, including psychic, sensors, combat and Controller runtime. Repeated native psychic traces also matched.
- Production pack verification, native `--build-check`, WASM launch preflight, launcher tests and source formatting passed.
- The explicit `phoenix_psychic_smoke` target compiled. Its graphical journey could not run because Xvfb could not bind a sandbox socket. No graphical or screen-reader pass is claimed. The network service suite was likewise blocked by socket restrictions.

Native/WASM checks do not establish actual screen-reader, physical-device or six-display acceptance.
