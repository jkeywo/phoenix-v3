# Modern Cruiser fit

`base.cruiser` is the 300-metre House Caelifen six-station fit. Its physical ship-local X axis is forward: collision half-width is 150 metres and half-length is 70 metres. Render height is 90 metres, calibrated against the broadside drawing. Artwork is an illustrated approximation across bearings, not a geometry bake.

## Equipment and ownership

| Watch       | Fitted responsibilities                                                                                                  |
| ----------- | ------------------------------------------------------------------------------------------------------------------------ |
| Captain     | One-star design: alerts, eligible AI Console stances and an Objectives list with advisory priority selection (new GUI pending) |
| Helm        | Two drive assemblies, steering, lateral thrust, boost and impulse                                                        |
| Tactical    | Five kinetic turrets, eight flank PDCs, six four-tube missile banks, magazines, Flight operations and two Security teams |
| Engineering | One reactor/core network, distribution, local reserves, cooling, finite craft ammunition supplies and four repair teams  |
| Science     | Broad radar, focused scanners, four shield sectors, qualified displacement, two Medical teams and a four-place infirmary |
| Comms       | Powered radio, conversations, psychic choir, away decisions and Dispatch                                                 |

Comms also owns the [psychic choir](psychic-choir.md): Thalia Venn sustains Veil, Severin Ash projects phantom ships and Mara Sable wards one ship. Their separate choir seats use ordinary personnel travel and Medical availability. A dedicated reserve, supply link and cooling link connect the apparatus to Engineering. Simple and Middle expose planning intent with ordinary AI execution; Detailed exposes each power directly.

The auxiliary Navigator Console remains attached to Science and controls FTL mission departure. Captain's [revised design](bridge-command.md#captain-design-agreement--23-october-2026) has one star, no machinery reserve and no Command Duty Officer roster; the Objectives GUI, priority integration and obsolete-fitting removal remain pending. Other stations keep their ordinary complexity profiles, Controller authority, permitted observations and department-scoped duty officers. Simple delegates shields and routine repairs; deeper profiles expose their controls. Vacant stations use their ordinary Controller policies.

Five main turrets share one material. Fore/aft dorsal and ventral mounts traverse 270 degrees; the central dorsal mount traverses 360 degrees. Main-gun and PDC ammunition is finite. Each four-tube missile bank selects between its own finite homing and havok magazines. Main weapons and PDC standby draw actual local power. The encounter injects no ammunition or resources between waves; ordinary finite onboard servicing continues.

Two utility shuttles occupy the utility bay. Four interceptors and two bombers occupy the combat bay. These are production entities using the carried-craft runtime, with separate identities, saved condition, orders, charging and finite replenishment. Tactical owns both bays; Engineering owns the carrier supply magazines. Bay launch/recovery remains serial per bay, using the existing forward clearance point above the hull. Returns brake above the carrier before docking. Sprite bay markers are registration references rather than a new launch-geometry contract.

The connected interior binds equipment and duties to engines, engineering, hangar, magazines, passage, Medical, sensors, bridge and weapons spaces. Science owns two Medical teams and their officers, with field treatment, stabilisation, escorted infirmary recovery and supported away contributions. Tactical owns two Security teams and their officers for away assignments and target-authored boarding. Damage can injure duty officers through the production casualty policy; ordinary repairs do not heal injuries or replace lost personnel.

The Cruiser can select the existing **Boarding action** exercise, with collision-safe target placement. Its utility shuttles obey the same below-half-hull boarding insertion rule as the existing finite transports; recovery does not repeat that insertion restriction. The Cruiser itself authors bridge-seizure and reactor-occupation encounters defended by its actual Security teams. Dispatch and Comms retain manifest and objective decisions. Local jumps and the three introductory missions remain deferred.

## Combat test

Select **Cruiser combat test** and **Cruiser — Six-station combat fit**. The first three raiders arrive after ten seconds; subsequent groups arrive thirty seconds after the preceding three raiders are destroyed. Objectives and shipwide advisories identify the current wave. Damage, heat, ammunition, repairs and craft commitments persist.

`base.cruiser-test-raider` inherits the existing pirate unchanged apart from fixed hostile faction membership. The encounter uses receipt-gated spawns and published condition objectives. Each spawned raider has its own stable handle; objective completion requires all three actual destruction facts. Save/recovery retains those handles and the inter-wave timer.

Victory completes the scenario after all nine raiders are destroyed, leaving the physical world running for recovery and deliberate FTL departure. Early departure reports withdrawal and the individual wave statuses, alongside ordinary craft/personnel dispositions. Destroying the Cruiser fails its active objective and the encounter.

## Assets and explicit checks

The hull and articulated turret use packaged calibrated depth and corresponding derived view-space normals. Eight bearings reuse five registered rectangles. Hull frames expose five turret sockets, eight PDC muzzles, 24 tube origins, two engine nozzles and two bay reference points. The shared turret exposes its mounting pivot and muzzle. Physical firing offsets remain independent of image-space registration and signed draw-order layers.

See [source-map provenance and regeneration](../art/cruiser/README.md). The image service supplied the hull depth draft but rejected turret generation; turret depth is an authored geometric approximation. Ordinary builds use tracked packaged textures and sidecars without `raw/`, Pillow or image generation.

The explicit `phoenix_cruiser_smoke` target loads production content, can render all eight assembled bearings, exercises craft orders and records mid-wave continuation. It is excluded from CTest and does not freeze the production balance:

```powershell
./scripts/build.ps1 -Target native -BuildTarget phoenix_cruiser_smoke
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --gallery 0
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --powered-gallery 0
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --headless 12000
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --transport
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --medical
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --boarding
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --withdrawal 0
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --frame 0
```

The transport exercise reuses the rescue operation with the Cruiser selected and the casualty placed 350 metres away for hull clearance. It checks separate shuttle and displacement deployment/recovery through four owning watches, including actual remote treatment by the contributed Medical team. The clinical exercise privately seeds onboard injuries, then checks treatment, stabilisation, infirmary recovery, injured Security treatment and continuation. The boarding exercise privately bounds encounter success/loss probabilities, then checks utility-shuttle insertion, Comms objectives, extraction and capture; it does not establish production boarding balance.

Withdrawal and frame capture restore the combat run's `build/cruiser-midwave.json`, requiring the same exact pack. Frame capture requests a short manual Helm manoeuvre to expose actual engine effects. The combat journey launches six combat craft after the first wave arrives, then recalls them at sixty seconds through Tactical. The browser build of the explicit target provides `phoenix_cruiser_smoke.html` for the powered eight-bearing material gallery. The separate native `phoenix_cruiser_ui_smoke` target opens Science and Tactical, selects a clinical map order through the ordinary Console controls, and captures the resulting panels; run it from `build/native` with argument `base.zip`.

See the [execution evidence and remaining acceptance limits](smoke/cruiser.md).

Record actual execution results separately; successful loading or a screenshot does not establish a completed three-wave journey, screen-reader usability or physical six-display performance.
