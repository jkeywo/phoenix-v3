# Shared property effects implementation record

23 October 2026. Implements the [second gameplay-engine migration](property-effects-migration-plan.md). Automated verification results are recorded below; graphical and platform acceptance remain separate.

## Delivered behavior

The effects catalogue supports reusable typed modifier definitions. Officer traits reference effects and duties bind their roles to native equipment. Existing assignments, travel, health and deployment determine maintained contributions from current authoritative state. Independent scenario and deferred shield-reaction applications retain their own identities and lifetimes. Restoring a world rebuilds maintained contributions without persisting or duplicating an officer aggregate cache.

Property consumers combine signed deltas once, then clamp in their native units. This covers propulsion, speed, turning, sensing, turret traverse, weapon cycling, magazine replenishment, shield recharge/off-state recharge, sustain, discharge, protection, repair, treatment, security and radio range. Comparable faults join output properties; they do not change sustain prices, shield discharge conversion or speed limits. Hard faults, destruction, structural availability and missing funding remain separate operating gates. Funded boost/impulse deltas join the sum with their existing payment and interruption rules.

Positive cycle rates sample a minimum-one-tick interval when firing; already committed cooldowns continue normally. A zero cycle rate blocks a new shot. Magazine replenishment stores milli-tick work, preserves progress while paused and resumes without resetting its batch. Team work preserves paid material commitments and cannot buy a new batch at zero output. Native supply fractions and multi-team participation act after comparable property composition.

Independent applications use stable source, definition, application and named-contribution identities. A modifier applied again through the same source/target handle refreshes its lifetime; a different definition cannot steal that active handle. Shield grants retain their deliberate-instance semantics. Source disappearance does not retract an independent application. Loss of the actual target shield contribution removes its attached modifiers at the reconciliation boundary. Both operations and complete reaction groups prepare changes before publishing them or paying.

Personnel presentation and assignment scoring consume compiled contributions. Personnel's multiplier describes the aggregate percentage before the native physical bound; mechanism observations retain their own permitted effective values. Ordinary observations do not expose hidden scenario sources. Workshop supports modifier effects, trait references and duty bindings. Production and development content use shared definitions; synthetic legacy fixture shorthand is lowered before entering the production loader.

World/session schema **19** includes independent property applications and magazine work precision. Unsupported earlier saves require a new session. Restore validates complete named application groups, known definitions, source/target consistency and compatible equipment before replacing the live world.

## Review and verification

The independent review identified mismatched speed-demand quotation, orphaned shield-target modifiers, Personnel projection bounds and aggregate arithmetic bounds. Corrections align quote/operation inputs, reconcile lost targets, align display contracts and saturate checked arithmetic before multiplication. A further audit corrected boarding factor display/restore limits for large security effects and retained 64-bit percentage formatting. Regression fixtures exercise those boundaries. Review also requested common shield/modifier lifecycle and preparation helpers, preserving the two native state owners. The expanded source re-review passed with no unresolved findings.

The independent agent returned **PASS** on the corrected source. Focused native cases passed for content, scenario World, Personnel, combat, paid flight, Medical, security, sensors and simulation. The scoped C++ formatting check and `git diff --check` passed. The native build check passed.

The complete native build passed. The final full CTest run passed **36/37** cases, including all migration cases; `system.away` failed in a concurrently added fixture using an unavailable `emit` action. Its first corrected retry reached a second fixture error: saving before admitted commands had executed. After those corrections, the rebuilt away suite passed, completing coverage of all **37** native suites across the full run and targeted retry. Those concurrent source edits are preserved and excluded from this migration's commit. An earlier full run passed 35/37 before separate away and conversation fixture corrections. These results are not presented as one uninterrupted green full-checkout run.

The WebAssembly build, launch preflight and all system suites passed. All **15 native/WASM continuation comparisons** passed across the gate and fresh paired executions. The gate initially stopped at Medical and then docking because generated native baselines predated the final native rebuild; fresh native and WebAssembly outputs matched exactly before those baselines were updated. A later mission comparison exposed a native test executable predating a concurrent timestamped-log fixture change. Rebuilding that native target produced an exact match, including the new log entry. No simulation changes were needed for these trace corrections. Active timed modifiers, officer transitions and pending reactions are included in the Medical and combat traces.

The network-service gate passed **12/12**, including native TLS/WebSocket and reconnect coverage. The migration-only base pack was independently verified through the production pack loader, excluding concurrent rescue content. Scoped C++, content and documentation formatting checks and whitespace checks passed.

## Limits

No graphical Personnel/Workshop journey, screen-reader run, physical-device check or performance smoke was performed for this change. Maintained contributions are derived from typed state when queried; representative platform profiling remains a separate measurement.

The remaining `fault_effectiveness` consumers belong to unmigrated mechanisms: beam payload damage, psychic work, craft/transport, logistics, FTL and native policy/diagnostic checks. Their continued presence does not represent a second officer modifier path. Resource/store/recipe unification, general abilities and triggers, psychic effects, weapon payloads and directed-system unification remain later stages.
