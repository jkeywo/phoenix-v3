# Resource identity migration

The maintained base, test and test-3d packs explicitly declare their resources; base also declares ammunition recipes. These three roots remain independent. Native energy and coolant use pack-local identities with unit sizes. The test-3d conversion binds eleven electrical stores and two coolant sources, including assembly overrides; coolant capacity equals the existing initial quantity. The test-missions child pack contains maps only and continues to use phoenix-base dependencies.

## Identity decisions

Existing named ammunition and material IDs are retained. Repeated named ammunition profiles were compared before conversion and were identical; there were no conflicting profile definitions to split. Resources also used as cargo retain their former cargo size and gain ammo size one. Anonymous courier and top/bottom duel magazines with no profile share base.ammunition.generic, preserving weapon fallback metadata. Anonymous duel homing, havok and PDC magazines receive distinct base.ammunition.duel-* identities to preserve their profiles. The anonymous test raider bomb magazine receives test.ammunition.raider.

| Fitting                                        | Resource                    |
| ---------------------------------------------- | --------------------------- |
| base.cruiser.systems.fore-dorsal-mag           | base.ammunition.kinetic     |
| base.cruiser.systems.fore-ventral-mag          | base.ammunition.kinetic     |
| base.cruiser.systems.aft-dorsal-mag            | base.ammunition.kinetic     |
| base.cruiser.systems.aft-ventral-mag           | base.ammunition.kinetic     |
| base.cruiser.systems.mid-dorsal-mag            | base.ammunition.kinetic     |
| base.cruiser.systems.bow-port-homing           | base.ammunition.homing      |
| base.cruiser.systems.bow-port-havok            | base.ammunition.havok       |
| base.cruiser.systems.bow-starboard-homing      | base.ammunition.homing      |
| base.cruiser.systems.bow-starboard-havok       | base.ammunition.havok       |
| base.cruiser.systems.port-fore-homing          | base.ammunition.homing      |
| base.cruiser.systems.port-fore-havok           | base.ammunition.havok       |
| base.cruiser.systems.port-aft-homing           | base.ammunition.homing      |
| base.cruiser.systems.port-aft-havok            | base.ammunition.havok       |
| base.cruiser.systems.starboard-fore-homing     | base.ammunition.homing      |
| base.cruiser.systems.starboard-fore-havok      | base.ammunition.havok       |
| base.cruiser.systems.starboard-aft-homing      | base.ammunition.homing      |
| base.cruiser.systems.starboard-aft-havok       | base.ammunition.havok       |
| base.cruiser.systems.pdc-port-1-mag            | base.ammunition.pdc         |
| base.cruiser.systems.pdc-port-2-mag            | base.ammunition.pdc         |
| base.cruiser.systems.pdc-port-3-mag            | base.ammunition.pdc         |
| base.cruiser.systems.pdc-port-4-mag            | base.ammunition.pdc         |
| base.cruiser.systems.pdc-starboard-1-mag       | base.ammunition.pdc         |
| base.cruiser.systems.pdc-starboard-2-mag       | base.ammunition.pdc         |
| base.cruiser.systems.pdc-starboard-3-mag       | base.ammunition.pdc         |
| base.cruiser.systems.pdc-starboard-4-mag       | base.ammunition.pdc         |
| base.cruiser.systems.interceptor-supply        | base.craft.interceptor      |
| base.cruiser.systems.bomber-supply             | base.craft.bomber           |
| base.armed-courier.systems.forward-mag         | base.ammunition.generic     |
| base.rescue-courier.systems.forward-mag        | base.ammunition.generic     |
| base.rescue-casualty.systems.forward-mag       | base.ammunition.generic     |
| base.interceptor.systems.intercept-mag         | base.craft.interceptor      |
| base.bomber.systems.bomb-mag                   | base.craft.bomber           |
| base.equipment.combat-top-mag                  | base.ammunition.generic     |
| base.equipment.combat-bottom-mag               | base.ammunition.generic     |
| base.equipment.combat-homing-mag               | base.ammunition.duel-homing |
| base.equipment.combat-havok-mag                | base.ammunition.duel-havok  |
| base.equipment.combat-pdc-mag                  | base.ammunition.duel-pdc    |
| test.carrier.interceptor.systems.intercept-mag | exercise.interceptor        |
| test.carrier.bomber.systems.bomb-mag           | exercise.bomber             |
| test.carrier.ship.systems.interceptor-supply   | exercise.interceptor        |
| test.carrier.ship.systems.bomber-supply        | exercise.bomber             |
| test.carrier.raider.systems.bomb-mag           | test.ammunition.raider      |
| test.logistics.ship.systems.interceptor-supply | exercise.interceptor        |
| test.logistics.ship.systems.bomber-supply      | exercise.bomber             |

The delivery cruiser inherits its magazine fittings while replacing the logistics store list. Its 27 magazine aliases explicitly repeat the inherited common stock definitions, including their initial quantities; they do not introduce empty cargo stores.

## Complete-batch audit

Generated recipe work equals the legacy refill duration at twenty work units per second. Output quantity equals the legacy refill amount, or capacity where amount was omitted. These are complete batches: a processor waits until the entire output fits. No amounts, durations or operating-energy rates were retuned.

| Fitting                                  | Capacity | Batch output | Work | Energy per second |
| ---------------------------------------- | -------: | -----------: | ---: | ----------------: |
| base.armed-courier.systems.forward-mag   |        4 |            4 |  100 |                 0 |
| base.rescue-courier.systems.forward-mag  |        4 |            4 |  100 |                 0 |
| base.rescue-casualty.systems.forward-mag |        4 |            4 |  100 |                 0 |
| base.equipment.combat-top-mag            |        6 |            6 |   80 |                 0 |
| base.equipment.combat-bottom-mag         |        6 |            6 |   80 |                 0 |
| base.equipment.combat-pdc-mag            |      300 |          100 |   80 |                 0 |
