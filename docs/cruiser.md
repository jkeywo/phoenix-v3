# Modern Cruiser fit

`base.cruiser` is the 300-metre House Caelifen six-station fit. Its physical ship-local X axis is forward: collision half-width is 150 metres and half-length is 70 metres. Render height is 90 metres, calibrated against the broadside drawing. Artwork is an illustrated approximation across bearings, not a geometry bake.

## Equipment and ownership

| Watch       | Fitted responsibilities                                                                                                 |
| ----------- | ----------------------------------------------------------------------------------------------------------------------- |
| Captain     | Alerts, Command stances and coordination                                                                                |
| Helm        | Two drive assemblies, steering, lateral thrust, boost and impulse                                                       |
| Tactical    | Five kinetic turrets, eight flank PDCs, six four-tube missile banks, magazines and Flight operations                    |
| Engineering | One reactor/core network, distribution, local reserves, cooling, finite craft ammunition supplies and four repair teams |
| Science     | Separate broad radar, forward/aft focused scanners, four overlapping shield sectors and qualified displacement          |
| Comms       | Powered radio, conversations, away decisions and Dispatch                                                               |

The auxiliary Navigator Console remains attached to Science and controls FTL mission departure. Each station keeps its ordinary complexity profiles, Controller authority, permitted observations and department-scoped duty officers. Simple delegates shields and routine repairs; deeper profiles expose their controls. Vacant stations use their ordinary Controller policies.

Five main turrets share one material. Fore/aft dorsal and ventral mounts traverse 270 degrees; the central dorsal mount traverses 360 degrees. Main-gun and PDC ammunition is finite. Each four-tube missile bank selects between its own finite homing and havok magazines. Main weapons and PDC standby draw actual local power; there is no between-wave ammunition refill.

Two utility shuttles occupy the utility bay. Four interceptors and two bombers occupy the combat bay. These are production entities using the carried-craft runtime, with separate identities, saved condition, orders, charging and finite replenishment. Tactical owns both bays; Engineering owns the carrier supply magazines. Bay launch/recovery remains serial per bay and launches vertically above the hull. Sprite bay markers are registration references rather than a new launch-geometry contract.

The connected interior binds equipment and duties to engines, engineering, hangar, magazines, passage, sensors, bridge and weapons spaces. A Medical compartment is reserved, but Medical facilities, officers, teams, injuries and treatment are deliberately excluded pending that separate implementation. Local jumps, psychic operations and security encounters are also deferred.

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
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --headless 12000
./build/native/Release/phoenix_cruiser_smoke.exe build/native/base.zip --transport
```

The transport exercise reuses the rescue operation with the Cruiser selected and the casualty placed 350 metres away for hull clearance. It checks separate shuttle and displacement deployment/recovery through the four owning watches. The browser build of the explicit target provides `phoenix_cruiser_smoke.html` for the eight-bearing material gallery.

Record actual execution results separately; successful loading or a screenshot does not establish a completed three-wave journey, screen-reader usability or physical six-display performance.
