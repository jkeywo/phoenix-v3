# Native combat continuation

Start with [continuation.cppm](continuation.cppm).

Owns schema validation and state-tree continuation for native combat compartments, mechanisms, shields and projectiles. Projectile restore receives immutable visual/payload/deck identities through an OrdnanceRestoreCatalogue; it cannot query a World. Native adapters attach restored values to ECS entities and retain rule-capture layout validation at the effects boundary.

Dependencies: Directed capability/combat state, shield definitions and the state-tree runtime. No graphics, network or Application dependency.

Validation: Existing synthetic combat, craft, controller and checkpoint continuation cases through real World paths after activation.
