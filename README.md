# Phoenix V3

[Courier station complexity](docs/courier-complexity.md) defines the common Courier fitting and its one-, two- and three-star Helm and Support workloads.

[Storage, logistics and docking](docs/storage-logistics.md) adds finite supplies, automatic replenishment, docked transfers, cargo shuttles and docking-supported away delivery.

[Psychic choir](docs/psychic-choir.md) adds multiple psychic Duty Officer seats, sustained cloaking, moving ghost contacts and wards under Comms.

[Security and boarding](docs/security-boarding.md) adds target-authored encounters, finite transports and whole-ship capture.

[The modern Cruiser](docs/cruiser.md) fits six watches, five kinetic turrets, eight PDCs, 24 missile tubes and eight carried craft, with Medical, Security, a three-wave pirate combat test and calibrated sprite materials.

[Carried craft](docs/carried-craft.md) adds shuttle bays, interceptor/bomber sorties, docked servicing, shuttle away transport and manual delivery using the crew's ship.

[Mission objectives and debriefs](docs/mission-objectives.md) connect physical rescue outcomes, stable events and authoritative departure reports through authored rules.

[Controller model providers](docs/controller-models.md) adds optional AnyJev, llama.cpp and Jev decision providers through the shared action executor, with per-Controller host configuration and automatic state-tree fallback.

[Navigator FTL departure](docs/ftl-departure.md) adds a dedicated auxiliary Console, a powered and cooled FTL reserve, deliberate departure and a persistent report for that ship's crew.

[Away missions](docs/away-missions.md) adds Dispatch, qualified displacement, Station-owned contributions and the Rescue under threat demonstration.

[Duty officers](docs/duty-officers.md) adds department-scoped Personnel rosters, compartment travel, expertise modifiers and named AI internal-message speakers.

The Comms conversation increment adds nearby hails, authored branching/group dialogue and a two-pane player Console. See [Comms conversations](docs/comms-conversations.md) for the demonstration and authoring contract.

A fresh rewrite of Project Phoenix, using the previous project as a design and implementation reference, in the Settled Dark setting.

Start with the **[consolidated Game Design Document](docs/GDD.md)**. It defines the intended gameplay, crew responsibilities, Settled Dark setting, authoring behavior and validation scenarios for Phoenix V3, with coverage of all 216 reviewed catalogue entries and 76 architecture decisions.

The runtime foundation uses **C++20 + Flecs + raylib**, with **RmlUi player consoles/custom radars**, **Dear ImGui docking for Workshop/GM**, and **Emscripten for WebAssembly**. See the [technical stack](docs/technical-stack.md), [implementation sequence](docs/implementation-plan.md), [build instructions](docs/building.md) and [faster build iteration](docs/build-performance.md). The foundation is under active implementation; the full GDD is not a list of completed features.

Linux builds of the native game and command-line packager use `./scripts/build-linux.sh`; see [Linux prerequisites, checks and current platform limits](docs/building.md#linux-native-game-and-packager).

The [testing policy](docs/testing.md) uses three categories: manually invoked smoke captures, content verification without frozen tuning values, and automated system integration tests using synthetic fixtures. Equipment tuning and player workload require prototyping and playtesting. Historical implementation claims refer to Phoenix V2 and do not describe working V3 features.

The [Console Controller architecture](docs/controller-architecture.md) separates human/AI decision-making from physical Systems through shared blackboards and Controller-owned command sections. All live command decisions use that common runtime. Complexity profiles choose operators; physical mechanisms consume commands and report facts. The [refactoring plan](docs/controller-refactor-plan.md) records implementation and verification evidence. See [Controller authoring](docs/controller-content.md) for channels, subscriptions, observation scope, messaging and compatibility.

The [shared gameplay engine design](docs/gameplay-engine-design.md) describes the target effect, ability, resource and template architecture across equipment, officers and scenarios. Its [migration notes](docs/gameplay-engine-migration.md) start with shields and track delivery boundaries separately from the finished design.

[Bridge Command](docs/bridge-command.md) adds authored alert conditions, AI Console stances and a compact interactive Viewscreen sidebar. Couriers default to Command beside the scene, with alert selection on Support.

The [gameplay phase](docs/gameplay-implementation.md) records the original fitted equipment, power/heat/coolant, Helm, knowledge and scenario implementation. The Controller refactor retains those mechanisms while replacing System decision trees and station presets with Controller trees and Console profiles. [Gameplay Workshop](docs/gameplay-workshop.md) edits the current definitions and runs isolated native/browser tests. Verification and remaining physical-device acceptance are recorded separately.

The [Workshop](docs/workshop.md) authors maps, entities and Consoles, previews live widgets, and exports verified mod packs with dependencies and explicit overrides. [Network play](docs/network-play.md) supports native/browser fleet members, approved crew and read-only GM inspection, bounded content/projection transfers, and portable recovery. See [MVP status and evidence](docs/mvp-status.md) for completed behavior and the remaining physical-device, reader and public-network acceptance checks.

The native [main menu and pre-game lobby](docs/native-menu.md) provide cascading setup, ship/player/GM join forms, installed pack management, persistent settings and managed saves. Join-code invitations admit crews automatically; players claim Consoles and ready individually before a five-second launch countdown.

[Courier Race and Navigation](docs/courier-race.md) adds two playable Couriers, shipwide race objectives, a shared waypoint and a navigation map with pan, zoom and reset. Human-seeking consoles follow available crew, and each ship receives its own race result.

The [comic renderer](docs/comic-rendering.md) uses calibrated sprite depth for lighting and per-pixel occlusion, with depth-backed sun shadows, crosshatching and directional engine light. The Phoenix Base Game pack includes the Courier, powered engineering fit, six-Console Cruiser and Rescue under threat. Enable the bundled Test Missions child mod for Free Flight and the six other experimental missions. Use the development-only test base for powered objective scenarios and navigation/lighting exercises; see the [reusable content guide](docs/gameplay-content.md). Workshop provides a live material/lighting preview. Browser graphics require WebGL 2.

The [combat MVP](docs/combat-mvp.md) extends the [first combat stage](docs/combat-stage-one.md) with sustained beams, selectable ammunition, elevated homing/havok missiles, point defence, layered shields and fault-aware repairs in the asteroid duel. Verification and remaining platform acceptance are recorded separately.

[Sensors and focused scanning](docs/sensors.md) adds capability-based radar, selectable bearing-only long-range contacts, independently targeted sensor turrets, timed discoveries and retained observations. Long-range blips can be assigned to scanners and weapons, including independent homing missiles. Courier Tactical combines broad identity/fire-arc coverage with a narrow scanner for shields, compartment condition and scenario findings.

The [source guide](docs/source-guide.md) explains the inputs; the [prior setting agreement](docs/settled-dark-direction.md) preserves the earlier pivot decisions. The consolidated GDD incorporates those decisions and the subsequent V3 consolidation choices. Historical snapshots remain unchanged.

See [launch bases and development test content](docs/base-mods.md) for fixed base selection, child mods, isolated preferences and the model test level.

The [first combat stage](docs/combat-stage-one.md) adds an armed Courier versus pirate asteroid duel, projectile weapons, separate turrets/magazines, structural compartments, repair teams and optional radar fire arcs.

## Hosted browser build

[Open Phoenix V3](https://jkeywo.github.io/phoenix-v3/). The [HTML main menu](docs/browser-menu.md) matches native and loads the appropriate WASM build at lobby or Workshop entry. Host Game offers scenarios from the selected production base; Join Game separates new ships, players and GMs. QR invitations go directly to the chosen runtime. Native and browser multiplayer default to the hosted Northflank signalling service. Advanced Connection Settings allow a custom service.

Every push to `main` builds, verifies and deploys the browser apps through [GitHub Actions](https://github.com/jkeywo/phoenix-v3/actions/workflows/build.yml). See [hosting details](docs/building.md#github-pages).

## Reference material

- [Old federated GDD](references/phoenix-v2/docs/gdd/index.md).
- [Gameplay redesign](references/phoenix-v2/docs/gameplay-redesign/README.md), including the [216-system review](references/phoenix-v2/docs/gameplay-redesign/system-review.md).
- [Increment plan and partial implementation status](references/phoenix-v2/docs/gameplay-redesign/implementation/README.md), [visual plan](references/phoenix-v2/docs/gameplay-redesign/implementation/index.html) and [migration ledger](references/phoenix-v2/docs/gameplay-redesign/implementation/migration-ledger.json).
- [Shared Settled Dark world](references/settled-dark/shared-world/world.md) and [chronology](references/settled-dark/shared-world/chronology/eras.md).

References are historical snapshots. They retain their original wording, including V2 commands, instructions, authority statements and links to files that may only exist in the original repository. They do not automatically become V3 requirements. The [source manifest](references/source-manifest.json) records original paths and SHA-256 hashes.

[Teams and Medical](docs/medical-teams.md) adds authored generic team orders, clinical teams, officer conditions, facility recovery and optional away treatment.
