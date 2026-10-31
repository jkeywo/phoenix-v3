# Repair planning and finite material policy

Start with [repairs.cppm](repairs.cppm).

Owns local structural/fault task priorities, travel/arrival diagnosis, stacked work arithmetic, purchase rounding and work-credit planning. Controller policies select destinations from permitted reports and observations. Local task selection receives only the assigned compartment of the physical structure; it never chooses a remote destination. Routes, property-adjusted effort and current stock availability are typed inputs; earlier workers may still change later workers through the ordinary effect transaction boundary.

The module never batches or reorders effect payment/commit operations and has no ECS or World access.

Validation: Existing synthetic native combat/controller/continuation cases through the production World adapter after activation. These sources are prepared privately until that integration gate.
