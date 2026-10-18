# Phoenix V3

A fresh rewrite of Project Phoenix, using the previous project as a design and implementation reference, in the Settled Dark setting.

Start with the **[consolidated Game Design Document](docs/GDD.md)**. It defines the intended gameplay, crew responsibilities, Settled Dark setting, authoring behavior and validation scenarios for Phoenix V3, with coverage of all 216 reviewed catalogue entries and 76 architecture decisions.

The runtime foundation uses **C++20 + Flecs + raylib**, with **RmlUi player consoles/custom radars**, **Dear ImGui docking for Workshop/GM**, and **Emscripten for WebAssembly**. See the [technical stack](docs/technical-stack.md), [implementation sequence](docs/implementation-plan.md), [build instructions](docs/building.md) and [faster build iteration](docs/build-performance.md). The foundation is under active implementation; the full GDD is not a list of completed features.

The [testing policy](docs/testing.md) uses three categories: manually invoked smoke captures, content verification without frozen tuning values, and automated system integration tests using synthetic fixtures. Equipment tuning and player workload require prototyping and playtesting. Historical implementation claims refer to Phoenix V2 and do not describe working V3 features.

The [Workshop](docs/workshop.md) authors maps, entities and Consoles, previews live widgets, and exports verified mod packs with dependencies and explicit overrides. [Network play](docs/network-play.md) supports native/browser fleet members, approved crew and read-only GM inspection, bounded content/projection transfers, and portable recovery. See [MVP status and evidence](docs/mvp-status.md) for completed behavior and the remaining physical-device, reader and public-network acceptance checks.

The native [main menu and pre-game lobby](docs/native-menu.md) provide cascading setup, ship/player/GM join forms, installed pack management, persistent settings and managed saves. Approved crews ready individually before a five-second launch countdown.

The [comic renderer](docs/comic-rendering.md) uses calibrated sprite depth for lighting and per-pixel occlusion, with depth-backed sun shadows, crosshatching and directional engine light. The training pack includes the Courier and a `training.lighting` demonstration; Workshop provides a live material/lighting preview. Browser graphics require WebGL 2.

The [source guide](docs/source-guide.md) explains the inputs; the [prior setting agreement](docs/settled-dark-direction.md) preserves the earlier pivot decisions. The consolidated GDD incorporates those decisions and the subsequent V3 consolidation choices. Historical snapshots remain unchanged.

## Hosted browser build

[Open Phoenix V3](https://jkeywo.github.io/phoenix-v3/). The [HTML main menu](docs/browser-menu.md) matches native and loads the appropriate WASM build at lobby or Workshop entry. Host Game offers verified training scenarios; Join Game separates new ships, players and GMs. QR invitations go directly to the chosen runtime. Multiplayer requires a separately hosted WSS signalling service under Advanced Connection Settings.

Every push to `main` builds, verifies and deploys the browser apps through [GitHub Actions](https://github.com/jkeywo/phoenix-v3/actions/workflows/build.yml). See [hosting details](docs/building.md#github-pages).

## Reference material

- [Old federated GDD](references/phoenix-v2/docs/gdd/index.md).
- [Gameplay redesign](references/phoenix-v2/docs/gameplay-redesign/README.md), including the [216-system review](references/phoenix-v2/docs/gameplay-redesign/system-review.md).
- [Increment plan and partial implementation status](references/phoenix-v2/docs/gameplay-redesign/implementation/README.md), [visual plan](references/phoenix-v2/docs/gameplay-redesign/implementation/index.html) and [migration ledger](references/phoenix-v2/docs/gameplay-redesign/implementation/migration-ledger.json).
- [Shared Settled Dark world](references/settled-dark/shared-world/world.md) and [chronology](references/settled-dark/shared-world/chronology/eras.md).

References are historical snapshots. They retain their original wording, including V2 commands, instructions, authority statements and links to files that may only exist in the original repository. They do not automatically become V3 requirements. The [source manifest](references/source-manifest.json) records original paths and SHA-256 hashes.
