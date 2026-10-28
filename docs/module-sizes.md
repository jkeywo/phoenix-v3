# Source size inventory

This inventory records the final ownership cohort and includes-only header deduplication. It is a source/reading snapshot, **not a final runtime or platform acceptance receipt**. See [migration evidence](modularisation-evidence.md) for checks actually run.

- Scope: current first-party C++ files beneath `src/` (`.cpp`, `.cppm`, `.hpp`, `.cc`, `.cxx`, `.h`, `.hxx`). Tests, historical references, vendored dependencies and generated build outputs are excluded. CMake, Markdown and external web assets are excluded. All maintained platform variants count, even when one binary selects only native or browser implementations.
- Physical lines include comments, blank lines, directives and embedded JavaScript/JSON/text inside C++. Nonblank lines count lines whose whitespace-stripped text is nonempty; comments still count. These are reproducible reading sizes, not comment-stripped logical statements.
- A module row is a family/module folder. One folder can own several named modules/targets; executable and private composition folders can have no primary named module. Partitions are counted separately from primary modules. Target independence is not inferred from these counts.
- The four maintained World `native_headers.hpp` files contain shared standard/third-party includes only. They count as authored source, not build-generated implementation. Native Runtime bodies compile in ordinary CPP units, not class-body includes.

Totals: **644 files**, **134 module folders**, **211 primary named modules + 4 private partitions**, **145,547 physical lines**, **142,605 nonblank lines**.

The source fingerprint hashes sorted relative paths and UTF-8 contents with line endings normalized to LF: `c1f5eb6c5301bc0b77f1a1e88395e529048d150c0d14a80d2e157fff55426989`. Recompute after any gate-driven source fix; CMake-only edits do not change this snapshot.

Machine-readable [module sizes](module-sizes.csv) and [individual files](module-files.csv) provide the same counts. The latter includes per-file normalized hashes.

## Families

| Family        | Folders | Files | Primary / private partitions | Physical | Nonblank |
| ------------- | ------: | ----: | ---------------------------: | -------: | -------: |
| `application` |       6 |    12 |                        6 / 0 |    1,440 |    1,406 |
| `behavior`    |       2 |     6 |                        2 / 0 |    2,108 |    2,094 |
| `content`     |      17 |    73 |                       27 / 0 |   21,175 |   20,969 |
| `control`     |       5 |    26 |                       11 / 0 |    2,922 |    2,836 |
| `effects`     |       8 |    45 |                       22 / 0 |    7,026 |    6,752 |
| `executables` |       7 |    17 |                        5 / 0 |    2,531 |    2,491 |
| `foundation`  |       4 |    11 |                        4 / 0 |    1,882 |    1,858 |
| `gameplay`    |      23 |   108 |                       51 / 0 |   12,459 |   12,278 |
| `graphics`    |       5 |    22 |                        6 / 0 |    4,476 |    4,421 |
| `knowledge`   |       4 |    26 |                        4 / 0 |    4,588 |    4,489 |
| `networking`  |      10 |    32 |                       14 / 0 |    9,458 |    9,342 |
| `platform`    |       7 |    36 |                       10 / 0 |    4,990 |    4,897 |
| `resources`   |       4 |    12 |                        5 / 0 |    3,967 |    3,918 |
| `scenarios`   |       5 |    20 |                        6 / 0 |    5,101 |    5,057 |
| `ui`          |      12 |    53 |                       18 / 0 |   20,250 |   19,934 |
| `workshop`    |       9 |    45 |                       14 / 0 |   10,135 |    9,915 |
| `world`       |       6 |   100 |                        6 / 4 |   31,039 |   29,948 |

## Modules within each family

Read the module README for its public front door and dependency direction. Large World binding totals deliberately expose remaining cross-domain integration policy; ordinary compilation does not imply every binding is thin.

### application

| Module                                                            | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [connections](../src/application/connections/README.md)           |     2 |       87 |       81 |                        1 / 0 |
| [displays](../src/application/displays/README.md)                 |     2 |      195 |      188 |                        1 / 0 |
| [library](../src/application/library/README.md)                   |     2 |      705 |      701 |                        1 / 0 |
| [preference-store](../src/application/preference-store/README.md) |     2 |      138 |      130 |                        1 / 0 |
| [preferences](../src/application/preferences/README.md)           |     2 |      139 |      134 |                        1 / 0 |
| [shell](../src/application/shell/README.md)                       |     2 |      176 |      172 |                        1 / 0 |

### behavior

| Module                                             | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [state-tree](../src/behavior/state-tree/README.md) |     4 |    1,224 |    1,215 |                        1 / 0 |
| [vocabulary](../src/behavior/vocabulary/README.md) |     2 |      884 |      879 |                        1 / 0 |

### content

| Module                                                | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [assets](../src/content/assets/README.md)             |     6 |    1,477 |    1,466 |                        2 / 0 |
| [catalog](../src/content/catalog/README.md)           |     2 |      191 |      181 |                        1 / 0 |
| [combat](../src/content/combat/README.md)             |     5 |    1,555 |    1,544 |                        2 / 0 |
| [controllers](../src/content/controllers/README.md)   |     6 |    2,520 |    2,503 |                        2 / 0 |
| [distribution](../src/content/distribution/README.md) |     3 |      708 |      696 |                        1 / 0 |
| [documents](../src/content/documents/README.md)       |     3 |    1,193 |    1,180 |                        1 / 0 |
| [effects](../src/content/effects/README.md)           |     9 |    3,295 |    3,265 |                        2 / 0 |
| [equipment](../src/content/equipment/README.md)       |     6 |    1,516 |    1,496 |                        2 / 0 |
| [knowledge](../src/content/knowledge/README.md)       |     4 |    1,856 |    1,845 |                        2 / 0 |
| [logistics](../src/content/logistics/README.md)       |     5 |      791 |      784 |                        2 / 0 |
| [operations](../src/content/operations/README.md)     |     4 |    1,154 |    1,148 |                        2 / 0 |
| [packs](../src/content/packs/README.md)               |     2 |      489 |      483 |                        1 / 0 |
| [resources](../src/content/resources/README.md)       |     4 |      744 |      732 |                        2 / 0 |
| [scenarios](../src/content/scenarios/README.md)       |     3 |      847 |      841 |                        1 / 0 |
| [sensing](../src/content/sensing/README.md)           |     6 |      738 |      728 |                        2 / 0 |
| [ui](../src/content/ui/README.md)                     |     3 |      979 |      967 |                        1 / 0 |
| [verification](../src/content/verification/README.md) |     2 |    1,122 |    1,110 |                        1 / 0 |

### control

| Module                                                | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [controllers](../src/control/controllers/README.md)   |     6 |      508 |      493 |                        3 / 0 |
| [coordination](../src/control/coordination/README.md) |     6 |      622 |      607 |                        3 / 0 |
| [execution](../src/control/execution/README.md)       |     6 |    1,117 |    1,097 |                        1 / 0 |
| [ownership](../src/control/ownership/README.md)       |     4 |      319 |      295 |                        2 / 0 |
| [providers](../src/control/providers/README.md)       |     4 |      356 |      344 |                        2 / 0 |

### effects

| Module                                                | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [abilities](../src/effects/abilities/README.md)       |     6 |      711 |      679 |                        3 / 0 |
| [contracts](../src/effects/contracts/README.md)       |     2 |      644 |      605 |                        1 / 0 |
| [funding](../src/effects/funding/README.md)           |     2 |      445 |      433 |                        1 / 0 |
| [lifecycle](../src/effects/lifecycle/README.md)       |    10 |    1,175 |    1,131 |                        5 / 0 |
| [processes](../src/effects/processes/README.md)       |     8 |    1,196 |    1,147 |                        4 / 0 |
| [properties](../src/effects/properties/README.md)     |     4 |      706 |      677 |                        2 / 0 |
| [rules](../src/effects/rules/README.md)               |     7 |      813 |      779 |                        3 / 0 |
| [transactions](../src/effects/transactions/README.md) |     6 |    1,336 |    1,301 |                        3 / 0 |

### executables

| Module                                                            | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [browser](../src/executables/browser/README.md)                   |     1 |      277 |      273 |                        0 / 0 |
| [diagnostics](../src/executables/diagnostics/README.md)           |     5 |      301 |      294 |                        2 / 0 |
| [inference-bridge](../src/executables/inference-bridge/README.md) |     1 |      193 |      192 |                        0 / 0 |
| [native](../src/executables/native/README.md)                     |     5 |    1,194 |    1,177 |                        2 / 0 |
| [pack](../src/executables/pack/README.md)                         |     1 |       78 |       76 |                        0 / 0 |
| [verify](../src/executables/verify/README.md)                     |     1 |       35 |       33 |                        0 / 0 |
| [workshop-host](../src/executables/workshop-host/README.md)       |     3 |      453 |      446 |                        1 / 0 |

### foundation

| Module                                                           | Files | Physical | Nonblank | Primary / private partitions |
| ---------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [console-controls](../src/foundation/console-controls/README.md) |     2 |      615 |      611 |                        1 / 0 |
| [contracts](../src/foundation/contracts/README.md)               |     3 |      626 |      620 |                        1 / 0 |
| [data](../src/foundation/data/README.md)                         |     4 |      350 |      343 |                        1 / 0 |
| [input](../src/foundation/input/README.md)                       |     2 |      291 |      284 |                        1 / 0 |

### gameplay

| Module                                                               | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [away](../src/gameplay/away/README.md)                               |    10 |    1,863 |    1,842 |                        4 / 0 |
| [boarding](../src/gameplay/boarding/README.md)                       |     4 |      246 |      238 |                        2 / 0 |
| [combat](../src/gameplay/combat/README.md)                           |     8 |      659 |      638 |                        4 / 0 |
| [combat-continuation](../src/gameplay/combat-continuation/README.md) |     2 |      484 |      481 |                        1 / 0 |
| [conversations](../src/gameplay/conversations/README.md)             |     3 |    1,013 |    1,008 |                        1 / 0 |
| [craft](../src/gameplay/craft/README.md)                             |     3 |      387 |      379 |                        1 / 0 |
| [damage](../src/gameplay/damage/README.md)                           |     2 |      200 |      200 |                        1 / 0 |
| [directed](../src/gameplay/directed/README.md)                       |     2 |      307 |      307 |                        1 / 0 |
| [docking](../src/gameplay/docking/README.md)                         |     3 |      440 |      431 |                        1 / 0 |
| [fitting](../src/gameplay/fitting/README.md)                         |     2 |      235 |      228 |                        1 / 0 |
| [flight](../src/gameplay/flight/README.md)                           |     9 |    1,937 |    1,919 |                        4 / 0 |
| [ftl](../src/gameplay/ftl/README.md)                                 |     5 |      456 |      445 |                        2 / 0 |
| [logistics](../src/gameplay/logistics/README.md)                     |     4 |      605 |      597 |                        2 / 0 |
| [machinery](../src/gameplay/machinery/README.md)                     |     2 |      132 |      128 |                        1 / 0 |
| [ordnance](../src/gameplay/ordnance/README.md)                       |     2 |      368 |      365 |                        1 / 0 |
| [ordnance-contacts](../src/gameplay/ordnance-contacts/README.md)     |     2 |      176 |      174 |                        1 / 0 |
| [personnel](../src/gameplay/personnel/README.md)                     |    10 |      562 |      534 |                        5 / 0 |
| [projectiles](../src/gameplay/projectiles/README.md)                 |     2 |      145 |      144 |                        1 / 0 |
| [psychic](../src/gameplay/psychic/README.md)                         |    12 |      728 |      718 |                        6 / 0 |
| [repairs](../src/gameplay/repairs/README.md)                         |     2 |      193 |      193 |                        1 / 0 |
| [sensors](../src/gameplay/sensors/README.md)                         |    16 |    1,046 |    1,034 |                        8 / 0 |
| [teams](../src/gameplay/teams/README.md)                             |     1 |       41 |       39 |                        1 / 0 |
| [weapons](../src/gameplay/weapons/README.md)                         |     2 |      236 |      236 |                        1 / 0 |

### graphics

| Module                                         | Files | Physical | Nonblank | Primary / private partitions |
| ---------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [assets](../src/graphics/assets/README.md)     |     2 |      128 |      123 |                        1 / 0 |
| [audio](../src/graphics/audio/README.md)       |     4 |      602 |      591 |                        2 / 0 |
| [geometry](../src/graphics/geometry/README.md) |     2 |      105 |      100 |                        1 / 0 |
| [radar](../src/graphics/radar/README.md)       |     4 |      801 |      789 |                        1 / 0 |
| [scene](../src/graphics/scene/README.md)       |    10 |    2,840 |    2,818 |                        1 / 0 |

### knowledge

| Module                                                | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [consoles](../src/knowledge/consoles/README.md)       |     2 |    1,097 |    1,091 |                        1 / 0 |
| [projections](../src/knowledge/projections/README.md) |     2 |      141 |      133 |                        1 / 0 |
| [read-models](../src/knowledge/read-models/README.md) |    19 |    1,846 |    1,772 |                        1 / 0 |
| [records](../src/knowledge/records/README.md)         |     3 |    1,504 |    1,493 |                        1 / 0 |

### networking

| Module                                                       | Files | Physical | Nonblank | Primary / private partitions |
| ------------------------------------------------------------ | ----: | -------: | -------: | ---------------------------: |
| [consensus](../src/networking/consensus/README.md)           |     2 |      994 |      989 |                        1 / 0 |
| [durability](../src/networking/durability/README.md)         |     2 |      120 |      115 |                        1 / 0 |
| [fleet](../src/networking/fleet/README.md)                   |     5 |    1,162 |    1,149 |                        2 / 0 |
| [inference](../src/networking/inference/README.md)           |     2 |      220 |      218 |                        1 / 0 |
| [lobby](../src/networking/lobby/README.md)                   |     4 |    1,946 |    1,941 |                        2 / 0 |
| [local-session](../src/networking/local-session/README.md)   |     5 |    1,515 |    1,499 |                        2 / 0 |
| [projection](../src/networking/projection/README.md)         |     3 |      519 |      513 |                        1 / 0 |
| [remote-session](../src/networking/remote-session/README.md) |     4 |    1,923 |    1,910 |                        2 / 0 |
| [session-api](../src/networking/session-api/README.md)       |     2 |      396 |      357 |                        1 / 0 |
| [wire](../src/networking/wire/README.md)                     |     3 |      663 |      651 |                        1 / 0 |

### platform

| Module                                                   | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [accessibility](../src/platform/accessibility/README.md) |     4 |    1,258 |    1,246 |                        1 / 0 |
| [devices](../src/platform/devices/README.md)             |     5 |      426 |      419 |                        2 / 0 |
| [files](../src/platform/files/README.md)                 |     7 |      706 |      691 |                        2 / 0 |
| [lifecycle](../src/platform/lifecycle/README.md)         |     1 |      102 |       97 |                        1 / 0 |
| [storage](../src/platform/storage/README.md)             |    11 |    1,122 |    1,090 |                        2 / 0 |
| [transport](../src/platform/transport/README.md)         |     6 |    1,328 |    1,306 |                        1 / 0 |
| [viewport](../src/platform/viewport/README.md)           |     2 |       48 |       48 |                        1 / 0 |

### resources

| Module                                              | Files | Physical | Nonblank | Primary / private partitions |
| --------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [equipment](../src/resources/equipment/README.md)   |     2 |      699 |      681 |                        1 / 0 |
| [networks](../src/resources/networks/README.md)     |     4 |    1,803 |    1,786 |                        1 / 0 |
| [processors](../src/resources/processors/README.md) |     3 |      768 |      762 |                        2 / 0 |
| [stocks](../src/resources/stocks/README.md)         |     3 |      697 |      689 |                        1 / 0 |

### scenarios

| Module                                                    | Files | Physical | Nonblank | Primary / private partitions |
| --------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [environment](../src/scenarios/environment/README.md)     |     5 |      911 |      899 |                        2 / 0 |
| [integration](../src/scenarios/integration/README.md)     |     2 |    1,361 |    1,355 |                        1 / 0 |
| [interdictions](../src/scenarios/interdictions/README.md) |     4 |      601 |      599 |                        1 / 0 |
| [objectives](../src/scenarios/objectives/README.md)       |     4 |      259 |      250 |                        1 / 0 |
| [runtime](../src/scenarios/runtime/README.md)             |     5 |    1,969 |    1,954 |                        1 / 0 |

### ui

| Module                                                         | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [console-model](../src/ui/console-model/README.md)             |     8 |    2,864 |    2,840 |                        1 / 0 |
| [console-surface](../src/ui/console-surface/README.md)         |     8 |    7,948 |    7,785 |                        1 / 0 |
| [engineering-widgets](../src/ui/engineering-widgets/README.md) |     2 |      250 |      248 |                        1 / 0 |
| [flight-widgets](../src/ui/flight-widgets/README.md)           |     2 |      138 |      135 |                        1 / 0 |
| [information-widgets](../src/ui/information-widgets/README.md) |     2 |      120 |      118 |                        1 / 0 |
| [interaction](../src/ui/interaction/README.md)                 |     4 |      485 |      468 |                        2 / 0 |
| [operations-widgets](../src/ui/operations-widgets/README.md)   |     2 |      779 |      763 |                        1 / 0 |
| [preview](../src/ui/preview/README.md)                         |     2 |      366 |      360 |                        1 / 0 |
| [runtime](../src/ui/runtime/README.md)                         |     8 |    2,914 |    2,864 |                        3 / 0 |
| [screens](../src/ui/screens/README.md)                         |    10 |    3,013 |    2,989 |                        4 / 0 |
| [tactical-widgets](../src/ui/tactical-widgets/README.md)       |     2 |      189 |      186 |                        1 / 0 |
| [viewscreen](../src/ui/viewscreen/README.md)                   |     3 |    1,184 |    1,178 |                        1 / 0 |

### workshop

| Module                                                         | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [gameplay-editors](../src/workshop/gameplay-editors/README.md) |    11 |    3,281 |    3,225 |                        1 / 0 |
| [gm](../src/workshop/gm/README.md)                             |     2 |      193 |      188 |                        1 / 0 |
| [layout-editor](../src/workshop/layout-editor/README.md)       |     2 |      868 |      860 |                        1 / 0 |
| [map-editor](../src/workshop/map-editor/README.md)             |     4 |      406 |      396 |                        2 / 0 |
| [preview](../src/workshop/preview/README.md)                   |     4 |      603 |      591 |                        2 / 0 |
| [shell](../src/workshop/shell/README.md)                       |     9 |    1,383 |    1,328 |                        3 / 0 |
| [test-client](../src/workshop/test-client/README.md)           |     4 |      405 |      394 |                        2 / 0 |
| [tree-editor](../src/workshop/tree-editor/README.md)           |     2 |      907 |      885 |                        1 / 0 |
| [workflow](../src/workshop/workflow/README.md)                 |     7 |    2,089 |    2,048 |                        1 / 0 |

### world

| Module                                            | Files | Physical | Nonblank | Primary / private partitions |
| ------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [bindings](../src/world/bindings/README.md)       |    77 |   26,095 |   25,157 |                        0 / 1 |
| [lifecycle](../src/world/lifecycle/README.md)     |     9 |    3,081 |    2,961 |                        2 / 1 |
| [persistence](../src/world/persistence/README.md) |     5 |      695 |      679 |                        1 / 1 |
| [schedule](../src/world/schedule/README.md)       |     5 |      342 |      337 |                        1 / 1 |
| [spatial](../src/world/spatial/README.md)         |     2 |      427 |      421 |                        1 / 0 |
| [state](../src/world/state/README.md)             |     2 |      399 |      393 |                        1 / 0 |
