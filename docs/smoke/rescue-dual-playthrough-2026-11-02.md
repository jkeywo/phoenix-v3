# Rescue under Threat: Courier and Cruiser

Manually requested native, unattended gameplay runs, started 2 November 2026 and continued into 3 November. Courier completed the required mission. Cruiser was still blocked at the 8:05.00 diagnostic cutoff; no terminal grade was recorded.

## Setup and evidence boundaries

Actual `base.rescue-under-threat` content; slot `courier` fitted with `base.courier` and then `base.cruiser` in independent worlds. Every Console remained vacant and ordinary Controller trees owned AI work. No human claims, injected machinery commands, extra objectives or modified physical starting states were used. Schema 52; both task plans used seed 219071. Courier selected routine delivery, one Concealed Opportunist and the relief variation; Cruiser selected perishable delivery, Screen and Boarder, and relief.

The observation host called production `World.load`, `World.tick` and `World.save`. Captures retain five-second observations, thirty-second decision samples and sixty-second full checkpoints. Full decision diagnostics were obtained from separate restored copies, with exact restoration and state-preserving inspection checks. An explicitly recorded extra tick in an inspection copy never advances the running mission.

This used the existing Windows x64 Release native gameplay cache built after gameplay baseline `735b60f7`, plus a freshly rebuilt Controller decision library and observation executable for the navigation correction. Concurrent cold-start/session-loading and build-performance work was excluded from the frozen host. This is not a clean-checkout rebuild or the complete native/WASM validation gate. It supplies no graphical, browser, accessibility, device or performance acceptance evidence. Concurrent hosts/builds make elapsed wall time unsuitable as a production performance measurement.

## Navigation correction made during the requested assessment

The original Courier attempt reached the rescue region but rejected the boarding objective's mission parameter as outside contact/docking scope, diverted to Cairn, delivered the original consignment at 4:17.35, and stayed docked. The original Cruiser attempt also remained incomplete. Those baseline captures were stopped at 6:40 and 5:05 respectively; they are not completed rescue missions.

The user clarified the intended coordination chain: scenario objective coordinates → Navigation's prioritised waypoint → automatic ship-blackboard publication → Helm's own prioritised course → propulsion. Commit `50708934` implements that chain using existing shared waypoint commands and typed record bindings. The two Navigation widgets share one reusable planning policy. Helm uses matching shared points for charted destinations, or permitted contact guidance; an active waypoint no longer vetoes higher-priority flight work. Mission names remain contextual parameters rather than suppressing independently scoped boarding/navigation evidence. Generic boarding cancellation/undocking handles an approach to a different ship. No rescue-specific branch or new mission objective was added.

Native Controller, combat and docking integrations passed, including synthetic no-contact waypoint travel, unknown-destination refusal, matching contact targeting, mission-bearing docking, separate Navigation/Helm evaluation phases, interrupted objective continuation and checkpoint restore. Production verification accepted the corrected archive: 58 entity definitions, one map and 64 Consoles. Changed C++ and JSON/Markdown formatting checks passed. An intermediate direct-Helm waypoint implementation was superseded and its partial captures are not final playthrough evidence.

## Corrected Courier timeline

- 0:11.65 — approach completes and distress is published.
- 0:13.70 — the repair operation becomes available; the scenario publishes boarding/preparation objectives the next tick.
- Dispatch opens the operation and coordinates Medical, Repair and Security contributions plus a specialist through the ordinary manifest workflow. The assigned insertion mode is ship docking. The relief job stages a separate repair team.
- By 0:30 — Navigation has selected the high-priority boarding objective, plotted Morrow's published `(950000, 2500000)` position, and Helm/propulsion are approaching it. Close-in obstruction detours precede docking.
- 2:11.70 — Concealed Opportunist pirate branch starts.
- 3:26.65 — Courier docks and deploys the prepared rescue party to Morrow.
- 4:48.10 — reactor secured before the five-minute emergency, so that emergency is suppressed in this run.
- 5:28.15 — wounded saved; the party proceeds to propulsion repair.
- 6:59.75 — away work completes as `complete:repairs`.
- 7:00.75 — repair party and visiting personnel recovered, with no away crew deaths. The docking connection is separating during recovery and gone by 7:05. Morrow receives its departure objective. Navigation and Helm return to the original Cairn delivery objective 8.
- 7:30.75 — Morrow escapes under FTL and the rescue branch succeeds. The charted departure point is route guidance, not a required jump location.
- After the escape — Courier attacks the withdrawing raider with cannons and torpedoes, then uses impulse on the clear leg to Cairn. The HTN emits a Lone Raider replan after the original encounter has finished; it does not spawn a replacement in the observed interval.
- By 9:05 — docked at Cairn. The ship uses the original delivery objective and one admitted material transfer, with finite batches accounted for in transit.
- 10:02.25 — Cairn receives all 100 units and the delivery objective completes. The HTN root, delivery and rescue are succeeded; pirate/relief branches are superseded. The Courier's required mission is complete. Its repair party has no away crew deaths and the hull has recovered to its initial 1920 HP.

## Corrected Cruiser timeline

- 1:25.50 — distress arrives after the initial approach.
- 1:27.55 — repair operation becomes available; boarding/preparation follow the next tick.
- By 1:30 — Navigation and Helm select the rescue destination.
- 3:25.55 — Screen and Boarder arrives: boarder 15, screens 16 and 17.
- 5:00 — reactor emergency and the five-minute delivery reminder fire while the repair party remains aboard the Cruiser.
- 5:21.75 — interceptor 6 is destroyed during carrier manoeuvres, before any pirate shots were recorded. The trace also records hull damage to the Cruiser; the exact cause of the fighter loss was not established.
- 5:24 — the Cruiser's attack changes the pirate fleet from watch to hostile. By 5:35, the Cruiser has fired 35 starboard tube shots and its logistics Controllers have requested homing ammunition. The two screen ships have not fired yet.
- By 6:00 — 59 Cruiser tube shots; two bombers are flying independently and have homing projectiles in flight against the boarder and a screen. Morrow remains intact and neither side has boarded it.
- At 6:40 the surviving interceptors are back aboard and the Cruiser resumes making progress toward Morrow. A new launch cycle begins by 6:55. Interceptor 7 is destroyed at 6:58.65, with another 147 points of Cruiser hull damage in that sampled interval and still no pirate shots. The exact impact pair is unconfirmed. Detouring resumes, even though the planned rescue destination has not changed.

- 8:05.00 — captured and stopped with the rescue and delivery branches still running. The closest sampled approach was 1347 metres, outside the 500-metre assisted docking selection range. Helm used 18 distinct temporary detour points while retaining the rescue objective. Morrow was intact and uncaptured; the rescue party remained aboard. Cairn received none of the Cruiser’s 100 perishable units, and neither shuttle carried a cargo assignment.
- Final combat totals: 109 Cruiser shots, 17 bomber shots, 0 pirate shots. 2 carried craft were lost. This is an unfinished run with diagnosed blockers, not an expired perishable consignment or an adjudicated mission loss.

## Issues found

### Concealed raider selects idle instead of approaching Morrow

At tick 3601, both approach and idle are feasible at Scripted High. The encounter's `ready/conceal` publication is accepted by generic Helm idle: score 1, versus -549000 for the 550-metre rendezvous. Helm selects idle; propulsion follows idle; the raider remains at its spawn position and never begins boarding. Its Veil Controller correctly selects concealment. Readiness/idle bindings need operation scope so a cloak instruction does not become a higher-priority ship-wide idle task. Reweighting every navigation action would conceal this scope error.

The raider makes no offensive shots in the observed interval. Courier fires 16 cannon rounds during its approach and uses cannons and torpedoes against the withdrawing raider after Morrow escapes. This run therefore does not demonstrate the intended sustained fight during the away mission, Veil break-contact manoeuvre, defensive PDC interception or stern attack.

### Cruiser lookout treats vertically separated fighters as blocking obstacles

At tick 4800, recovering fighters 8 and 9 are about 27 and 25 metres from the Cruiser in plan view, at altitudes 65.851 and 58.500 metres. The Cruiser is at altitude zero, with half-height 45 metres; fighter half-height is 0.5 metres. Both fighters' desired altitudes are also clear (62.5 and 58.5 metres). The 2D lookout expands the Cruiser's 220-metre summed half-dimensions by the fighter radius and margin, ignores altitude, and marks these own craft as obstructions. At tick 4801 the actual frozen factual input is `navigation.blocked=true`, `lateral_safe=false`, and Helm is continuing a detour. The carrier makes progress in gaps between sorties, then detours again. Align navigation hazard geometry with physical vertical separation and flight/recovery corridors; investigate the separate fighter losses rather than assuming every potential obstruction is harmless.

### Cruiser's impulse Controllers have no travel policy

Both fitted impulse drives remain at zero command. At tick 2401 their rescue/navigation candidates are `unsupported_behavior`; their selected action is readiness, preserving zero. The ship travels at 16.5 m/s during its clear initial approach rather than using the fitted impulse systems. This is a missing reusable flight-policy binding, rather than a lack of rescue coordinates or missing impulse equipment.

### Cruiser's cargo owner has no delivery AI

The perishable branch admits 100 units and selects the Cruiser because it has usable cargo shuttle capacity. Its `cargo-hold` and logistics operation are owned by `engineering/logistics`, whose widget has no AI policy. The repair Controllers' transfer bindings lack authority over that store. `engineering/utility-bay` rejects the delivery objective as `unsupported_behavior` in the tick 2401 inspection, and neither utility shuttle is assigned cargo. The existing delivery candidate requests connected supply; it does not substitute a native cargo-shuttle decision. The fitting needs an ordinary, authorised logistics/cargo policy regardless of the navigation fix.

### Screen boarder knows the docking port but rejects Morrow's contact category

At tick 7201, the boarder has a current Morrow report identifying a Pack Mule, with category `contact`. Docking has current evidence for `1/fore`, about 149.5 metres away. The Helm `board` binding nevertheless requires the category `ship` and is inapplicable; the board objective does not supply a separate destination. Helm selects the lower-priority rendezvous course, so the docking binding cannot consume a matching boarding course. This is separate from the Concealed Opportunist's idle arbitration. Use independently permitted docking evidence or fix the authored category rather than broadening every unknown contact into a boardable ship.

### Raider PDCs have a readiness policy but no interception action

The inspected screen ship's tactical scope includes missile tracks at tick 7201, and physical homing projectiles are in flight against it. Its PDC rejects the standing interception behaviour as unsupported and selects readiness. Both delivery-raider PDC widget variants have only the readiness binding, despite fitting target and mode commands. Reuse the scoped interception policy for this fitting. The screen's main offensive weapons remain on standby because the Cruiser is outside their permitted contact scope; they are a separate case from the missing PDC policy.

### Screen encounter pressure is gated on player proximity

The screen fleet approaches Morrow but stays in `watch` while the Cruiser remains farther than 600 metres from the casualty and has not attacked. Boarding objectives remain withdrawn meanwhile. In this run the Cruiser's first attack, rather than its arrival, ended the watch at 5:24. This explains the delay in boarding/fighting pressure; it is an authored encounter trigger/tuning finding.

## Retained artifacts

All large captures remain in ignored `build/`. Baselines use `rescue-full-{courier,cruiser}-2026-11-02`; final corrected attempts use `rescue-navigation-{courier,cruiser}-2026-11-02`. Each final prefix has `.start.json`, `.trace.jsonl`, `.decisions.jsonl`, `.checkpoint.json`, `.state.json`, `.summary.json` and `.log`. Final minute checkpoints are in `rescue-navigation-checkpoints-2026-11-02/`.

Key restored decision snapshots: `rescue-navigation-raider-3601.json`, `rescue-navigation-cruiser-detour-4801.json`, `rescue-navigation-inspect-cruiser.json` (tick 2401), `rescue-navigation-boarder-7201.json`, and `rescue-navigation-screen-7201.json`.

| Artifact                                              | SHA-256                                                            |
| ----------------------------------------------------- | ------------------------------------------------------------------ |
| `rescue-playthrough-2026-11-02-base.zip`              | `9a79d022551851470130fff501a078857e3f0deb12aba0167a9676f920478b96` |
| `rescue-playthrough-2026-11-02-navigation.zip`        | `240cee5fbbb4e6c58da5501e8a0cdc5688b17fdb4efeaff5beb2f95a7756a8ca` |
| `native/Release/phoenix_rescue_playthrough_probe.exe` | `0291542359c93e372b44df28b451324774d4e7a1c344ad0464e0720882e939fa` |
| `native/Release/phoenix_rescue_playthrough_fixed.exe` | `dede61c0c7d3711d7da169c24659c1954ccf26cef80376cd6b07a3c46e8fe047` |

The observation host source is `rescue-playthrough-2026-11-02.cpp`. Its arguments are archive, allowed ship definition, tick limit and capture prefix. Simulation time is 20 ticks per second.

The final Courier capture is 10:10.00 (stopped after root success); the final Cruiser capture is 8:05.00 (diagnostic cutoff). Both hosts saved normally. Final world-state SHA-256: Courier `a29e389e431a1d1bf26d1554950737d5d7785d5a3415ec13d63ec5fa0624a242`, Cruiser `8cb3a6e0cbab2c5a4a6e606081376dd7e3e8b01486a1c6995b5765c73f804060`.
