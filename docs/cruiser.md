# Modern Cruiser fit

`base.cruiser` is the 300-metre House Caelifen six-station fit. Its physical ship-local X axis is forward: collision half-width is 150 metres and half-length is 70 metres. Render height is 90 metres, calibrated against the broadside drawing. Artwork is an illustrated approximation across bearings, not a geometry bake.

## Equipment and ownership

| Watch       | Fitted responsibilities                                                                                                                  |
| ----------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Captain     | One-star design: alerts, eligible AI Console stances and an Objectives list with advisory priority selection                             |
| Helm        | Two drive assemblies, steering, lateral thrust, boost and impulse                                                                        |
| Tactical    | Five kinetic turrets, eight flank PDCs, six four-tube missile banks, local magazines, combat craft, Security and general Dispatch        |
| Engineering | Reactor/core network, distribution, reserves, cooling, main supplies, four repair teams, utility shuttles and Engineering Dispatch       |
| Science     | Broad radar, focused scanners, four shield sectors, qualified displacement, two Medical teams, infirmary, Phantom choir and Psychic ward |
| Comms       | Powered radio, conversations, away dialogue and disclosure of station supply offers                                                      |

Science owns Phantom choir and Psychic ward, with separate specialist seats and Engineering supply/cooling. One star automatically wards the Cruiser; two stars permits direct power choices and targeting. Veil belongs to Courier Support.

The auxiliary Navigator remains attached to Science. Science defaults to one star; Comms and Captain have fixed one-star workloads. Captain operates alerts, eligible AI stances and its Objectives list with advisory navigation priority. It has no machinery reserve or Command Duty Officers. See the [complete remaining-station contract](cruiser-science-comms-captain.md) and [Helm, Tactical and Engineering workloads](cruiser-complexity.md).

Five main turrets share one material. Fore/aft dorsal and ventral mounts traverse 270 degrees; the central dorsal mount traverses 360 degrees. Main-gun and PDC ammunition is finite. Each four-tube missile bank selects between its own finite homing and havok magazines. Main weapons and PDC standby draw actual local power. The encounter injects no ammunition or resources between waves; ordinary finite onboard servicing continues.

Two utility shuttles occupy Engineering's utility bay. Four interceptors and two bombers occupy Tactical's combat bay. These are production entities using the carried-craft runtime, with separate identities, saved condition, orders, charging and finite replenishment. Engineering owns the carrier supply magazines. Bay launch/recovery remains serial per bay, using the existing forward clearance point above the hull. Returns brake above the carrier before docking. Sprite bay markers are registration references rather than a new launch-geometry contract. The [station workload contract](cruiser-complexity.md) defines the revised three-star controls, departmental mission contributions and depot resupply missions.

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
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --gallery 0 build/native/test-missions.zip
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --powered-gallery 0 build/native/test-missions.zip
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --headless 12000 build/native/test-missions.zip
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --watch 1000 build/native/test-missions.zip
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --transport
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --supply build/native/test-missions.zip
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --medical
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --boarding 0 build/native/test-missions.zip
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --withdrawal 0 build/native/test-missions.zip
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --frame 0 build/native/test-missions.zip
```

The transport exercise reuses the rescue operation with the Cruiser selected and the casualty placed 350 metres away for hull clearance. It checks separate shuttle and displacement deployment/recovery through four owning watches, including actual remote treatment by the contributed Medical team. The clinical exercise privately seeds onboard injuries, then checks treatment, stabilisation, infirmary recovery, injured Security treatment and continuation. The boarding exercise privately bounds encounter success/loss probabilities, then checks utility-shuttle insertion, Comms objectives, extraction and capture; it does not establish production boarding balance.

Withdrawal and frame capture restore the combat run's `build/cruiser-midwave.json`, requiring the same exact pack. Frame capture requests a short manual Helm manoeuvre to expose actual engine effects. The combat journey launches six combat craft after the first wave arrives, then recalls them at sixty seconds through Tactical. The browser build of the explicit target provides `phoenix_cruiser_smoke.html` for the powered eight-bearing material gallery. The separate native `phoenix_cruiser_ui_smoke` target opens Science and Tactical, selects a clinical map order through the ordinary Console controls, and captures the resulting panels; run it from `build/native` with argument `base.zip`.

See the [execution evidence and remaining acceptance limits](smoke/cruiser.md).

For the three revised workload screens at 1080p, run `phoenix_cruiser_ui_smoke.exe base.zip test-missions.zip --workloads`. Its rendered controls also exercise Comms disclosure, Engineering dispatch and Tactical contribution. The separate supply journey uses a quiet private combat-map variant with depleted initial parts and medical stores; it exercises both negotiated offers, automatic manifest preparation, a Tactical escort and physical return/unloading with continuation checks. The bounded `--watch` journey checks occupied one-star combat and compartment repair, independently of the complete three-wave exercise. See the [workload verification receipt](smoke/cruiser-workloads.md).

Record actual execution results separately; successful loading or a screenshot does not establish a completed three-wave journey, screen-reader usability or physical six-display performance.
