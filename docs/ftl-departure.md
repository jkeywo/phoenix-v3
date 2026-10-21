# Navigator FTL departure

The dedicated **Navigator** auxiliary Console follows an available Science/Support operator, then Helm. It is separate from the Navigation map and requires no extra lobby seat. Open Navigator, turn the FTL battery on, wait for readiness, then select **Leave mission**. Charging competes for real reactor power. Keeping a full reserve ready continues to consume power and generate heat.

Initial authored fittings target about 30 seconds from empty at full supply. Charging draws about half nominal reactor output; charging losses dissipate a fifth of that flow. A full reserve dissipates about a quarter of reactor output. Switching off stops incoming power and discharges the reserve over about 30 seconds, converting its remaining energy to heat. These are editable content defaults, not balance assertions. The basic Courier now uses inertial machinery and a compact FTL power supply; larger powered ships connect the passage assembly to their existing supply.

## Departure and report

Departure requires an enabled, operational, fully charged drive. It has no location, destination, fuel, shield or qualified-officer requirement in this increment. A displacement transfer that has actually started blocks departure. Queued transfers can be cancelled by departing. Deployed personnel produce a warning and may deliberately be left behind; departure never teleports them aboard.

The authoritative tick rechecks readiness after consuming admitted Controller changes. A refused click never becomes a queued future jump. A successful jump consumes the reserve once, removes that ship and its equipment from active physical queries, and closes its gameplay command admission. Other ships and committed frame numbering continue. Projectiles already fired remain physical objects.

Every Console of the departing crew receives the same frozen **Departed via FTL** report. Maps without authored debrief rules list crew-published objectives with their actual status and known teams/officers left behind. Maps with authored rules receive an authoritative snapshot of selected objectives, world facts, personnel disposition and the first matching result rule. See [mission objectives and debriefs](mission-objectives.md). Unfinished work is not automatically a defeat. Later race resolution cannot replace this report, and the departed ship cannot win by arriving after departure. The existing Return to menu action remains explicit; displaying the report does not disconnect a simulation peer.

## Authoring and continuation

- `kind: "ftl"` equipment binds a dedicated `power_store` and specifies `charge_per_second`, `charging_loss_per_second`, `ready_loss_per_second` and `off_discharge_per_second`. Rates are positive; charging losses must be below charging input, and ready losses must fit the charge rate.
- Its reserve starts empty, has adequate charge/discharge limits, converts operation consumption to heat with `operation_heat_ppm: 1000000`, and has no additional fractional leakage. It cannot supply other stores or share its reserve with another consumer. Normal power links, cooling, thermal stops, structural disablement and equipment faults still apply.
- Navigator owns the ordinary `system_enabled` Controller channel. `ftl.depart` carries the fitted System ID, owning Controller ID/generation and selected Console, with no channel, request ID, arguments or payload. It is a one-shot admitted action. Engineering owns the assembly's supply isolation and cooling allocation.
- Workshop exposes the FTL capability and rates through its equipment editor. `base.assembly.ftl` and `base.equipment.ftl` provide reusable fittings; the starter supply is a separate assembly.
- Permitted Console observations include bounded `ftl` status and `mission_report` records. The enclosing World save remains schema 13 with additive departure and discharge-remainder fields. Older saves without a departure record remain active. Content identity checks remain in force.

This implements mission departure. Full Guild passage with destinations, route knowledge, finite transit fuel and qualified navigators remains a separate design requirement.

## Validation

`system.ftl` uses independent synthetic ships through production content loading, Controller admission, resources, scenario execution, network projection and save/recovery. It covers charging, losses, isolation, passive discharge, readiness revalidation, duplicate/stale inputs, displacement and personnel, two-crew continuation and race outcomes. Existing away integration reuses the same synthetic away fixture.

The rendered GUI journey is explicitly invoked:

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_ftl_smoke
./build/native/Release/phoenix_ftl_smoke.exe build/native
```

It captures `build/ftl-off.png`, `build/ftl-charging.png`, `build/ftl-ready.png` and `build/ftl-report.png`. Actual screen-reader, physical-device and six-display acceptance require separate manual runs.

Verified on 21 October 2026: all 28 native system cases, all eight network-service cases, the complete WASM gate and matching native/WASM FTL continuation passed. The final synthetic fixture explicitly saves a non-zero fractional discharge remainder. Production content passed packaging verification. The manually invoked Windows native journey displayed the Navigator at 1280 × 900, enabled its battery through the rendered control, charged it, held readiness with continuing power draw and rising heat, then departed and displayed the crew report. Its screenshots were visually inspected. This establishes the rendered/semantic action journey; it does not establish actual screen-reader, touch, gamepad or six-screen acceptance.
