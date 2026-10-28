# Native weapon loading and launch

Start with [weapons.cppm](weapons.cppm).

Owns ammunition profile selection, committed loading-mode duration, loading/unloading progress, finite-stock consumption and return reservations, launch commitment and projectile initialization. WeaponMagazines grants only magazine definitions, StockStore references and current operability; the owner cannot query an ECS or a World.

Target qualification and permitted contacts belong to directed/ordnance owners. Native adapters retain fitting lookup, effect fact capture, emissions and entity creation at their existing phase boundaries.

Dependencies: Directed/combat state and finite stock runtime. No network, rendering or Application dependencies.

Validation: Existing synthetic combat, resource-loading and controller continuation system cases after adapter activation; tuning/audio smoke remains separate.
