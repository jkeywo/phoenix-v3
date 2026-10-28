# Native equipment identity and availability

`fitting.cppm` exports `phoenix.gameplay.fitting`; link `phoenix_gameplay_fitting`. Equipment, EquipmentIndex, ResourceBinding and FlightRuntime are the canonical native component records. Read find_equipment/equipment_availability in `fitting.cpp` for addressed lookup and structural eligibility.

The index borrows child handles in authored ordinal order; it is not a second mutable source of equipment state. FittingsView and GameplayView provide bounded native views. NativeOwnerDefinitions exposes only the immutable sections an actor mechanism needs, rather than the full compiled catalogue.

Keep the returned optional view in a named variable while traversing `values`.
In C++20, `fittings(ship)->values` as a range expression leaves a dangling view
before iteration. Use an `if` or range-loop initializer to retain the optional.
Borrowed handles and component references must stay within the current native
operation and must not survive structural equipment changes.

Dependencies point to flight/resource execution and domain definitions/state, with structural fault arithmetic below the adapter. World owns installation and child lifetime. Existing fitting, resource, Controller and native effect cases exercise that production path; current migration/continuation evidence is recorded centrally.
