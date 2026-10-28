# Recipes and processor execution

`processors.cppm` exports `phoenix.resources.processors`. Link `phoenix_resources_processors_definitions` for definitions/state declarations and `phoenix_resources_processors` for ResourceProcessor execution. The runtime prepares ingredient/receiving claims, advances funded work, changes selection and saves/restores batch state in `processors.cpp`.

`native.cppm` separately exports `phoenix.resources.processing` (target `phoenix_resources_processing`): ShipProcessors groups fitted processors and tick-local work/energy quotes. Catalogue-dependent recipe/binding compilation belongs in `content/resources`; World installation and process funding remain above these generic mechanisms.

Preserve fractional work, claim identity and prepared-versus-committed costs. Existing processor/stock/allocation integration cases are the relevant gates; migration receipts provide executed native/browser/continuation status. Tick-local quotes must be rebuilt rather than replayed from a save.

`ResourceProcessor::projection` owns recipe labels/order, mode and active/progress/work/revision presentation from explicitly supplied selectable recipe identities. Native World integration filters permitted feeds first, performs the same per-recipe stock lookups, then supplies current manual-workload authority. The row builder acquires no native lookup or new observation capability.
