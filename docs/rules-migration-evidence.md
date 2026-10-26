# Shared rules migration evidence

26 October 2026. Implementation follows the [approved plan](rules-migration-plan.md), based on `b907e3a`, on `codex/shared-rules`. The foundation is committed as `84e8773` and runtime integration as `2cd4702`. Automated verification and independent reviews are complete. This receipt distinguishes code delivery, executed checks and remaining manual acceptance.

## Implemented behavior

The mod-extensible catalogue compiles shared triggers, typed expressions, costs, operation overrides and source bindings. It reuses state-tree scalar types and the common prepared effect transaction. Unsupported phases, unavailable event fields, invalid arithmetic and retired authoring fields receive diagnostics. Shipped officer traits and synthetic fixtures use shared maintained rules.

Native source owners retain lifetime authority. Logical clocks, application identities, prepaid credits and next-phase leases support overlapping maintained sources, pause, shortage-stop rearming and exact continuation. Static maintained applications are installed once; dynamic updates affect existing modifier values without refreshing finite shield, ability or periodic lifetimes. Native target changes withdraw the last successfully applied target first. Retirement closes dependent grants at phase boundaries without evaluating, activating or paying rules.

Ongoing costs join existing `ShipResources` allocations and `StockTransaction` staging. Electrical upkeep competes with native consumers; mixed input bundles preserve paid fractional work. Entry costs are paid once before upkeep begins, using the available native operating budget. No synthetic equipment, extra stock balance or fabricated discharge allowance was added.

Native damage geometry, bypass, equal-layer sharing and reserves remain authoritative. Reached shield, compartment and system hooks use frozen prepared batches; failed payments remove their transformations before final sharing. Post contexts contain actual outcomes, and secondary work stays scheduled. Shield post hooks retain original caster context even when a pre hook removes the contribution. Impact and missile destruction payloads capture separate source facts at launch. Native destruction notifies once at first death, independently of the existing later blast-processing phase.

Regions reuse native circle, rectangle and annulus intervals. Swept crossings run in movement order, including complete transits and departures from an initial boundary. Sampled membership owns maintained occupancy. Initial and spawned occupants enter once; restore does not replay entry.

World and portable session saves use schema 27. They preserve native reserves plus rule clocks, membership, actual modifier values/subsets, application ownership, captured contexts, pending jobs and bounded receipts. Malformed owner identities, credits, deadlines, captures and forged maintained rows reject atomically. A narrow identity-only fallback accepts existing officer/equipment bindings on unprocessed native hull deaths without restoring their eligibility or recreating missing effects.

Workshop supports the shared authoring fields. Console status uses existing feeds and personnel departments, localized generic rows, opaque identities and bounded public blocker codes. Hidden captured facts and raw private diagnostics are excluded.

## Independent review and simplification

Independent correctness review and subsequent re-review inspected the implementation against base `b907e3a`. All reported findings were fixed, including destruction capture/notification, aggregate entity hooks, periodic cadence and stop edges, owner identity, filtered modifier pruning, saved live values, malformed ownership, retirement closure and phase-boundary continuation. Final independent correctness and architecture reviews both passed at immutable commit `2cd4702`, with no further actionable gaps.

The separate architecture audit led to centralized canonical native-store metadata, removal of the duplicated mutable invocation copy from prepared transactions, reuse of the strict integer reader, and query-local source-availability caching. Static maintenance no longer clones and recommits native contribution rosters every tick. Native physics, resource allocation, sensing and effect lifetime owners were retained.

## Executed verification

- Native foundation checks passed for compiled expressions, named allocation, ordered geometry and native flight/scenario behavior.
- The complete shared rule suite passed after the runtime conversion, including paid leases, mixed resource credits, invalid quotes, swept regions, exact owner removal, finite lifetime preservation, dependent retirement and malformed continuation.
- The final complete native build and all 44 CTest suites passed. Production content verification passed through the content suite; the empty-world build check also passed, without constituting a graphical smoke run.
- The complete native gate passed, including 12 live-network service cases and native continuation trace generation. Live-network checks ran separately from expensive compilation.
- The complete threaded WebAssembly gate passed, including production launch preflight and all 21 native/WebAssembly continuation comparisons.
- Ordinary WebAssembly compiled and passed the rules, directed, combat, personnel, stock-effect, ability and psychic suites, plus the empty-world build check and production launch preflight. Ordinary rules, directed and combat continuation traces matched native.
- Repository formatting verification passed: 445 C/C++, 10 CMake, 11 PowerShell and 271 web/data/document files. The final documentation updates also passed formatting verification.

Final logs are `build/rules-native-final-build.log`, `build/rules-native-final-ctest.log`, `build/rules-native-gate.log`, `build/rules-threaded-gate-final.log`, `build/rules-ordinary-build.log`, `build/rules-ordinary-gate.log` and `build/rules-format-final.log`. Earlier failed or cancelled iterations remain in `build/` for diagnosis; they do not establish completion. Checks were executed on Windows; Linux execution was not exercised.

## Remaining acceptance

GUI journeys, screen-reader operation, physical touch/gamepad devices, six-screen rendering and performance smoke acceptance remain separate from these automated checks, under the repository testing policy. World and portable session schema 27 require a new session for incompatible older saves.
