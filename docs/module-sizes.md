# Source size inventory

This is a source/reading snapshot, **not a runtime or platform acceptance receipt**. See [migration evidence](modularisation-evidence.md) and [the refinement record](understandability-refactor.md) for checks actually run.

- Scope: first-party C++ under `src/`; tests, historical references, dependencies, generated output, CMake, Markdown and external web assets are excluded. All maintained platform variants count.
- Physical lines include blanks. Nonblank lines include comments, directives and embedded JavaScript/JSON/text; these are reading sizes rather than comment-stripped logical statements.
- A module row is a family/module folder, which can contain several named modules/targets. Private partitions are counted separately. Counts do not establish target independence.
- The World `native_headers.hpp` files contain standard/third-party includes only; ordinary CPP units own implementation.
- Reproduce with `python scripts/module-inventory.py --write`, then format the Markdown. The optional baseline reports growth without imposing a quota.

Totals: **773 files**, **152 module folders**, **228 primary named modules + 17 private partitions**, **180,626 physical lines**, **177,260 nonblank lines**.

Normalized source fingerprint (sorted paths and UTF-8 contents, CRLF normalized to LF): `aa0bb12857dc536128fe2e1e4ab918f978a7d997fa669e62c22a0603d68c7fea`.

Machine-readable [module sizes](module-sizes.csv) and [individual files/hashes](module-files.csv) contain the same counts.

## Families

| Family        | Folders | Files | Primary / private partitions | Physical | Nonblank |
| ------------- | ------: | ----: | ---------------------------: | -------: | -------: |
| `application` |       6 |    13 |                        6 / 0 |    1,918 |    1,882 |
| `behavior`    |       2 |     6 |                        2 / 0 |    2,302 |    2,288 |
| `content`     |      17 |    76 |                       27 / 0 |   24,830 |   24,594 |
| `control`     |       7 |    49 |                       15 / 0 |    9,234 |    9,089 |
| `effects`     |       8 |    45 |                       22 / 0 |    7,063 |    6,790 |
| `executables` |       9 |    22 |                        5 / 0 |    3,813 |    3,738 |
| `foundation`  |       4 |    12 |                        4 / 0 |    2,122 |    2,097 |
| `gameplay`    |      23 |   108 |                       51 / 0 |   12,806 |   12,625 |
| `graphics`    |       5 |    28 |                        7 / 0 |    5,895 |    5,828 |
| `knowledge`   |       4 |    42 |                       11 / 0 |    8,558 |    8,444 |
| `networking`  |      10 |    33 |                       14 / 0 |   10,543 |   10,423 |
| `platform`    |       7 |    36 |                       10 / 0 |    5,279 |    5,184 |
| `resources`   |       4 |    12 |                        5 / 0 |    4,008 |    3,957 |
| `scenarios`   |       5 |    21 |                        6 / 0 |    6,170 |    6,124 |
| `ui`          |      14 |    72 |                       22 / 0 |   25,451 |   25,089 |
| `workshop`    |      10 |    61 |                       15 / 0 |   16,109 |   15,824 |
| `world`       |      17 |   137 |                       6 / 17 |   34,525 |   33,284 |

## Modules within each family

Read the module README for its interface and dependency direction. Private World integration remains coupled native composition; the folder split does not claim every policy is an independent domain library.

### application

| Module                                                            | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [connections](../src/application/connections/README.md)           |     2 |       87 |       81 |                        1 / 0 |
| [displays](../src/application/displays/README.md)                 |     2 |      195 |      188 |                        1 / 0 |
| [library](../src/application/library/README.md)                   |     2 |      819 |      815 |                        1 / 0 |
| [preference-store](../src/application/preference-store/README.md) |     3 |      474 |      464 |                        1 / 0 |
| [preferences](../src/application/preferences/README.md)           |     2 |      148 |      143 |                        1 / 0 |
| [shell](../src/application/shell/README.md)                       |     2 |      195 |      191 |                        1 / 0 |

### behavior

| Module                                             | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [state-tree](../src/behavior/state-tree/README.md) |     4 |    1,412 |    1,403 |                        1 / 0 |
| [vocabulary](../src/behavior/vocabulary/README.md) |     2 |      890 |      885 |                        1 / 0 |

### content

| Module                                                | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [assets](../src/content/assets/README.md)             |     6 |    1,584 |    1,572 |                        2 / 0 |
| [catalog](../src/content/catalog/README.md)           |     2 |      340 |      328 |                        1 / 0 |
| [combat](../src/content/combat/README.md)             |     5 |    1,598 |    1,587 |                        2 / 0 |
| [controllers](../src/content/controllers/README.md)   |     8 |    3,363 |    3,333 |                        2 / 0 |
| [distribution](../src/content/distribution/README.md) |     3 |    1,021 |    1,006 |                        1 / 0 |
| [documents](../src/content/documents/README.md)       |     3 |    1,648 |    1,634 |                        1 / 0 |
| [effects](../src/content/effects/README.md)           |     9 |    3,301 |    3,271 |                        2 / 0 |
| [equipment](../src/content/equipment/README.md)       |     6 |    1,516 |    1,496 |                        2 / 0 |
| [knowledge](../src/content/knowledge/README.md)       |     4 |    2,424 |    2,412 |                        2 / 0 |
| [logistics](../src/content/logistics/README.md)       |     5 |      874 |      866 |                        2 / 0 |
| [operations](../src/content/operations/README.md)     |     4 |    1,241 |    1,235 |                        2 / 0 |
| [packs](../src/content/packs/README.md)               |     2 |      518 |      512 |                        1 / 0 |
| [resources](../src/content/resources/README.md)       |     4 |      735 |      723 |                        2 / 0 |
| [scenarios](../src/content/scenarios/README.md)       |     3 |      951 |      945 |                        1 / 0 |
| [sensing](../src/content/sensing/README.md)           |     6 |      879 |      868 |                        2 / 0 |
| [ui](../src/content/ui/README.md)                     |     4 |    1,340 |    1,325 |                        1 / 0 |
| [verification](../src/content/verification/README.md) |     2 |    1,497 |    1,481 |                        1 / 0 |

### control

| Module                                                    | Files | Physical | Nonblank | Primary / private partitions |
| --------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [command-shapes](../src/control/command-shapes/README.md) |     2 |       31 |       31 |                        1 / 0 |
| [controllers](../src/control/controllers/README.md)       |     7 |    1,120 |    1,099 |                        3 / 0 |
| [coordination](../src/control/coordination/README.md)     |     6 |    1,045 |    1,027 |                        3 / 0 |
| [decisions](../src/control/decisions/README.md)           |    21 |    5,517 |    5,465 |                        3 / 0 |
| [execution](../src/control/execution/README.md)           |     5 |      781 |      763 |                        1 / 0 |
| [ownership](../src/control/ownership/README.md)           |     4 |      317 |      293 |                        2 / 0 |
| [providers](../src/control/providers/README.md)           |     4 |      423 |      411 |                        2 / 0 |

### effects

| Module                                                | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [abilities](../src/effects/abilities/README.md)       |     6 |      711 |      679 |                        3 / 0 |
| [contracts](../src/effects/contracts/README.md)       |     2 |      644 |      605 |                        1 / 0 |
| [funding](../src/effects/funding/README.md)           |     2 |      445 |      433 |                        1 / 0 |
| [lifecycle](../src/effects/lifecycle/README.md)       |    10 |    1,215 |    1,171 |                        5 / 0 |
| [processes](../src/effects/processes/README.md)       |     8 |    1,158 |    1,110 |                        4 / 0 |
| [properties](../src/effects/properties/README.md)     |     4 |      706 |      677 |                        2 / 0 |
| [rules](../src/effects/rules/README.md)               |     7 |      813 |      779 |                        3 / 0 |
| [transactions](../src/effects/transactions/README.md) |     6 |    1,371 |    1,336 |                        3 / 0 |

### executables

| Module                                                            | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| `base-verifier`                                                   |     1 |      123 |      121 |                        0 / 0 |
| [browser](../src/executables/browser/README.md)                   |     4 |    1,123 |    1,090 |                        0 / 0 |
| [diagnostics](../src/executables/diagnostics/README.md)           |     5 |      301 |      294 |                        2 / 0 |
| [inference-bridge](../src/executables/inference-bridge/README.md) |     1 |      193 |      192 |                        0 / 0 |
| [native](../src/executables/native/README.md)                     |     5 |    1,375 |    1,358 |                        2 / 0 |
| [pack](../src/executables/pack/README.md)                         |     1 |       84 |       82 |                        0 / 0 |
| [verify](../src/executables/verify/README.md)                     |     1 |       56 |       53 |                        0 / 0 |
| `workshop-cli`                                                    |     1 |      183 |      180 |                        0 / 0 |
| [workshop-host](../src/executables/workshop-host/README.md)       |     3 |      375 |      368 |                        1 / 0 |

### foundation

| Module                                                           | Files | Physical | Nonblank | Primary / private partitions |
| ---------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [console-controls](../src/foundation/console-controls/README.md) |     2 |      700 |      696 |                        1 / 0 |
| [contracts](../src/foundation/contracts/README.md)               |     3 |      672 |      666 |                        1 / 0 |
| [data](../src/foundation/data/README.md)                         |     5 |      459 |      451 |                        1 / 0 |
| [input](../src/foundation/input/README.md)                       |     2 |      291 |      284 |                        1 / 0 |

### gameplay

| Module                                                               | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [away](../src/gameplay/away/README.md)                               |    10 |    1,899 |    1,877 |                        4 / 0 |
| [boarding](../src/gameplay/boarding/README.md)                       |     4 |      246 |      238 |                        2 / 0 |
| [combat](../src/gameplay/combat/README.md)                           |     8 |      666 |      645 |                        4 / 0 |
| [combat-continuation](../src/gameplay/combat-continuation/README.md) |     2 |      500 |      497 |                        1 / 0 |
| [conversations](../src/gameplay/conversations/README.md)             |     3 |    1,085 |    1,080 |                        1 / 0 |
| [craft](../src/gameplay/craft/README.md)                             |     3 |      403 |      395 |                        1 / 0 |
| [damage](../src/gameplay/damage/README.md)                           |     2 |      200 |      200 |                        1 / 0 |
| [directed](../src/gameplay/directed/README.md)                       |     2 |      324 |      324 |                        1 / 0 |
| [docking](../src/gameplay/docking/README.md)                         |     3 |      440 |      431 |                        1 / 0 |
| [fitting](../src/gameplay/fitting/README.md)                         |     2 |      235 |      228 |                        1 / 0 |
| [flight](../src/gameplay/flight/README.md)                           |     9 |    1,935 |    1,917 |                        4 / 0 |
| [ftl](../src/gameplay/ftl/README.md)                                 |     5 |      457 |      446 |                        2 / 0 |
| [logistics](../src/gameplay/logistics/README.md)                     |     4 |      608 |      600 |                        2 / 0 |
| [machinery](../src/gameplay/machinery/README.md)                     |     2 |      132 |      128 |                        1 / 0 |
| [ordnance](../src/gameplay/ordnance/README.md)                       |     2 |      390 |      387 |                        1 / 0 |
| [ordnance-contacts](../src/gameplay/ordnance-contacts/README.md)     |     2 |      180 |      178 |                        1 / 0 |
| [personnel](../src/gameplay/personnel/README.md)                     |    10 |      547 |      520 |                        5 / 0 |
| [projectiles](../src/gameplay/projectiles/README.md)                 |     2 |      145 |      144 |                        1 / 0 |
| [psychic](../src/gameplay/psychic/README.md)                         |    12 |      730 |      720 |                        6 / 0 |
| [repairs](../src/gameplay/repairs/README.md)                         |     2 |      188 |      188 |                        1 / 0 |
| [sensors](../src/gameplay/sensors/README.md)                         |    16 |    1,207 |    1,195 |                        8 / 0 |
| [teams](../src/gameplay/teams/README.md)                             |     1 |       41 |       39 |                        1 / 0 |
| [weapons](../src/gameplay/weapons/README.md)                         |     2 |      248 |      248 |                        1 / 0 |

### graphics

| Module                                         | Files | Physical | Nonblank | Primary / private partitions |
| ---------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [assets](../src/graphics/assets/README.md)     |     2 |      291 |      286 |                        1 / 0 |
| [audio](../src/graphics/audio/README.md)       |     7 |    1,215 |    1,197 |                        3 / 0 |
| [geometry](../src/graphics/geometry/README.md) |     3 |      220 |      211 |                        1 / 0 |
| [radar](../src/graphics/radar/README.md)       |     4 |      894 |      882 |                        1 / 0 |
| [scene](../src/graphics/scene/README.md)       |    12 |    3,275 |    3,252 |                        1 / 0 |

### knowledge

| Module                                                | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [consoles](../src/knowledge/consoles/README.md)       |     2 |    1,449 |    1,443 |                        1 / 0 |
| [projections](../src/knowledge/projections/README.md) |    17 |    3,279 |    3,258 |                        8 / 0 |
| [read-models](../src/knowledge/read-models/README.md) |    19 |    2,139 |    2,064 |                        1 / 0 |
| [records](../src/knowledge/records/README.md)         |     4 |    1,691 |    1,679 |                        1 / 0 |

### networking

| Module                                                       | Files | Physical | Nonblank | Primary / private partitions |
| ------------------------------------------------------------ | ----: | -------: | -------: | ---------------------------: |
| [consensus](../src/networking/consensus/README.md)           |     2 |      994 |      989 |                        1 / 0 |
| [durability](../src/networking/durability/README.md)         |     2 |      120 |      115 |                        1 / 0 |
| [fleet](../src/networking/fleet/README.md)                   |     5 |    1,176 |    1,163 |                        2 / 0 |
| [inference](../src/networking/inference/README.md)           |     2 |      238 |      236 |                        1 / 0 |
| [lobby](../src/networking/lobby/README.md)                   |     4 |    2,288 |    2,283 |                        2 / 0 |
| [local-session](../src/networking/local-session/README.md)   |     5 |    1,658 |    1,642 |                        2 / 0 |
| [projection](../src/networking/projection/README.md)         |     3 |      519 |      513 |                        1 / 0 |
| [remote-session](../src/networking/remote-session/README.md) |     4 |    2,099 |    2,085 |                        2 / 0 |
| [session-api](../src/networking/session-api/README.md)       |     3 |      519 |      478 |                        1 / 0 |
| [wire](../src/networking/wire/README.md)                     |     3 |      932 |      919 |                        1 / 0 |

### platform

| Module                                                   | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [accessibility](../src/platform/accessibility/README.md) |     4 |    1,258 |    1,246 |                        1 / 0 |
| [devices](../src/platform/devices/README.md)             |     5 |      453 |      446 |                        2 / 0 |
| [files](../src/platform/files/README.md)                 |     7 |      707 |      692 |                        2 / 0 |
| [lifecycle](../src/platform/lifecycle/README.md)         |     1 |      102 |       97 |                        1 / 0 |
| [storage](../src/platform/storage/README.md)             |    11 |    1,143 |    1,111 |                        2 / 0 |
| [transport](../src/platform/transport/README.md)         |     6 |    1,568 |    1,544 |                        1 / 0 |
| [viewport](../src/platform/viewport/README.md)           |     2 |       48 |       48 |                        1 / 0 |

### resources

| Module                                              | Files | Physical | Nonblank | Primary / private partitions |
| --------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [equipment](../src/resources/equipment/README.md)   |     2 |      692 |      674 |                        1 / 0 |
| [networks](../src/resources/networks/README.md)     |     4 |    1,813 |    1,796 |                        1 / 0 |
| [processors](../src/resources/processors/README.md) |     3 |      792 |      785 |                        2 / 0 |
| [stocks](../src/resources/stocks/README.md)         |     3 |      711 |      702 |                        1 / 0 |

### scenarios

| Module                                                    | Files | Physical | Nonblank | Primary / private partitions |
| --------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [environment](../src/scenarios/environment/README.md)     |     5 |      913 |      901 |                        2 / 0 |
| [integration](../src/scenarios/integration/README.md)     |     2 |    2,049 |    2,043 |                        1 / 0 |
| [interdictions](../src/scenarios/interdictions/README.md) |     3 |      493 |      491 |                        1 / 0 |
| [objectives](../src/scenarios/objectives/README.md)       |     4 |      261 |      252 |                        1 / 0 |
| [runtime](../src/scenarios/runtime/README.md)             |     7 |    2,454 |    2,437 |                        1 / 0 |

### ui

| Module                                                         | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [console-model](../src/ui/console-model/README.md)             |     9 |    3,472 |    3,441 |                        1 / 0 |
| [console-surface](../src/ui/console-surface/README.md)         |    11 |    8,601 |    8,431 |                        1 / 0 |
| [engineering-widgets](../src/ui/engineering-widgets/README.md) |     2 |      276 |      274 |                        1 / 0 |
| [flight-widgets](../src/ui/flight-widgets/README.md)           |     3 |      289 |      284 |                        1 / 0 |
| [information-widgets](../src/ui/information-widgets/README.md) |     5 |      851 |      839 |                        2 / 0 |
| [interaction](../src/ui/interaction/README.md)                 |     4 |      614 |      596 |                        2 / 0 |
| [navigation-panel](../src/ui/navigation-panel/README.md)       |     2 |      701 |      692 |                        1 / 0 |
| [operations-widgets](../src/ui/operations-widgets/README.md)   |     5 |    1,632 |    1,612 |                        1 / 0 |
| [preview](../src/ui/preview/README.md)                         |     2 |      583 |      576 |                        1 / 0 |
| [runtime](../src/ui/runtime/README.md)                         |     8 |    3,052 |    3,001 |                        3 / 0 |
| [screens](../src/ui/screens/README.md)                         |    10 |    3,205 |    3,181 |                        4 / 0 |
| [surface-host](../src/ui/surface-host/README.md)               |     5 |      265 |      263 |                        2 / 0 |
| [tactical-widgets](../src/ui/tactical-widgets/README.md)       |     3 |      512 |      507 |                        1 / 0 |
| [viewscreen](../src/ui/viewscreen/README.md)                   |     3 |    1,398 |    1,392 |                        1 / 0 |

### workshop

| Module                                                         | Files | Physical | Nonblank | Primary / private partitions |
| -------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [authoring-core](../src/workshop/authoring-core/README.md)     |    15 |    4,638 |    4,613 |                        1 / 0 |
| [gameplay-editors](../src/workshop/gameplay-editors/README.md) |    12 |    3,454 |    3,386 |                        1 / 0 |
| [gm](../src/workshop/gm/README.md)                             |     2 |      222 |      217 |                        1 / 0 |
| [layout-editor](../src/workshop/layout-editor/README.md)       |     2 |      944 |      930 |                        1 / 0 |
| [map-editor](../src/workshop/map-editor/README.md)             |     4 |      579 |      568 |                        2 / 0 |
| [preview](../src/workshop/preview/README.md)                   |     4 |      506 |      488 |                        2 / 0 |
| [shell](../src/workshop/shell/README.md)                       |     9 |    1,666 |    1,606 |                        3 / 0 |
| [test-client](../src/workshop/test-client/README.md)           |     4 |      421 |      410 |                        2 / 0 |
| [tree-editor](../src/workshop/tree-editor/README.md)           |     2 |    1,273 |    1,249 |                        1 / 0 |
| [workflow](../src/workshop/workflow/README.md)                 |     7 |    2,406 |    2,357 |                        1 / 0 |

### world

| Module                                                            | Files | Physical | Nonblank | Primary / private partitions |
| ----------------------------------------------------------------- | ----: | -------: | -------: | ---------------------------: |
| [combat-bindings](../src/world/combat-bindings/README.md)         |     8 |    2,774 |    2,676 |                        0 / 1 |
| [command-routing](../src/world/command-routing/README.md)         |    14 |    2,662 |    2,581 |                        0 / 1 |
| [crew-bindings](../src/world/crew-bindings/README.md)             |    14 |    4,832 |    4,607 |                        0 / 2 |
| [effect-applications](../src/world/effect-applications/README.md) |    11 |    1,880 |    1,787 |                        0 / 1 |
| [effect-lifecycle](../src/world/effect-lifecycle/README.md)       |    12 |    3,385 |    3,275 |                        0 / 1 |
| [effect-work](../src/world/effect-work/README.md)                 |    10 |    3,086 |    2,951 |                        0 / 1 |
| [lifecycle](../src/world/lifecycle/README.md)                     |     9 |    2,830 |    2,708 |                        2 / 1 |
| [native-actors](../src/world/native-actors/README.md)             |     4 |    1,233 |    1,209 |                        0 / 1 |
| [persistence](../src/world/persistence/README.md)                 |     5 |      706 |      690 |                        1 / 1 |
| [projection-bindings](../src/world/projection-bindings/README.md) |    11 |    2,278 |    2,228 |                        0 / 2 |
| [resource-bindings](../src/world/resource-bindings/README.md)     |     5 |      672 |      650 |                        0 / 1 |
| [scenario-bindings](../src/world/scenario-bindings/README.md)     |     7 |    1,453 |    1,413 |                        0 / 1 |
| [schedule](../src/world/schedule/README.md)                       |     5 |      394 |      388 |                        1 / 1 |
| [sensor-bindings](../src/world/sensor-bindings/README.md)         |     5 |    1,363 |    1,304 |                        0 / 1 |
| [spatial](../src/world/spatial/README.md)                         |     2 |      427 |      421 |                        1 / 0 |
| [state](../src/world/state/README.md)                             |     2 |      399 |      393 |                        1 / 0 |
| [travel-bindings](../src/world/travel-bindings/README.md)         |    13 |    4,151 |    4,003 |                        0 / 1 |
