# Phoenix V3

A fresh rewrite of Project Phoenix, using the previous project as a design and implementation reference, in the Settled Dark setting.

Start with the **[consolidated Game Design Document](docs/GDD.md)**. It defines the intended gameplay, crew responsibilities, Settled Dark setting, authoring behavior and validation scenarios for Phoenix V3, with coverage of all 216 reviewed catalogue entries and 76 architecture decisions.

The runtime foundation uses **C++20 + Flecs + raylib**, with **RmlUi player consoles/custom radars**, **Dear ImGui docking for Workshop/GM**, and **Emscripten for WebAssembly**. See the [technical stack](docs/technical-stack.md), [implementation sequence](docs/implementation-plan.md) and [build instructions](docs/building.md). The foundation is under active implementation; the full GDD is not a list of completed features.

The [testing policy](docs/testing.md) uses three categories: manually invoked smoke captures, content verification without frozen tuning values, and automated system integration tests using synthetic fixtures. Equipment tuning and player workload require prototyping and playtesting. Historical implementation claims refer to Phoenix V2 and do not describe working V3 features.

The [Workshop foundation](docs/workshop.md) authors map/entity/console data and exports verified ZIP packs, including dependency-based mods. The [local playable prototype](docs/local-play.md) loads selected packs, missions and ships, renders billboards, runs an authored radar/engine/steering console, and exports portable saves with paused local recovery. Networking and fleet consensus remain pending; see the implementation record for precise status.

The [source guide](docs/source-guide.md) explains the inputs; the [prior setting agreement](docs/settled-dark-direction.md) preserves the earlier pivot decisions. The consolidated GDD incorporates those decisions and the subsequent V3 consolidation choices. Historical snapshots remain unchanged.

## Reference material

- [Old federated GDD](references/phoenix-v2/docs/gdd/index.md).
- [Gameplay redesign](references/phoenix-v2/docs/gameplay-redesign/README.md), including the [216-system review](references/phoenix-v2/docs/gameplay-redesign/system-review.md).
- [Increment plan and partial implementation status](references/phoenix-v2/docs/gameplay-redesign/implementation/README.md), [visual plan](references/phoenix-v2/docs/gameplay-redesign/implementation/index.html) and [migration ledger](references/phoenix-v2/docs/gameplay-redesign/implementation/migration-ledger.json).
- [Shared Settled Dark world](references/settled-dark/shared-world/world.md) and [chronology](references/settled-dark/shared-world/chronology/eras.md).

References are historical snapshots. They retain their original wording, including V2 commands, instructions, authority statements and links to files that may only exist in the original repository. They do not automatically become V3 requirements. The [source manifest](references/source-manifest.json) records original paths and SHA-256 hashes.
