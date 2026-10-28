# Projectiles

`phoenix.gameplay.projectiles` owns the native projectile ECS query, stable-ID traversal, next identity, publication, flight frame, lifetime retirement, and continuation save/restore. Its dependencies are the weapons, ordnance contacts and combat continuation modules.

Launch preparation consumes the identity and firing ship random state before native payload captures. Publication remains a separate explicit step so capture failures retain the original timing. Restore validates the full structural roster first, sets the next identity, then validates authored captures and creates each entity in saved order, as before.

The guidance lookup supplies effective altitude and current target liveness immediately before advancing each projectile. The expiry operation invokes the existing native destruction path before boundary culling. Collision impact, pre/post damage reactions and blast chain traversal remain in the native adapter; this owner does not defer or reorder those effects.

The store and its cached query must be destroyed before the Flecs world. Native integration declares the world before the store. The store is registered and active. The final native and ordinary/threaded WASM project gates passed, including projectile and continuation cases; see the [verification record](../../../docs/modularisation-evidence.md).
