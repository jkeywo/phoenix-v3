# Spatial indexing and collision

Start with `spatial.cppm` (`phoenix.world.spatial`, target `phoenix_world_spatial`). It exposes bounded shapes, SpatialIndex and collision/sweep operations over physical records. The index uses caller-owned row indices rather than retaining Flecs component pointers. `collision.cpp` implements contact and swept collision response; the interface contains the small index implementation and visitor templates.

This module depends on World state/fixed arithmetic, not the World facade, content compilers or graphics. Callers provide actual start/end motion and deterministic row order; World decides when integration, collision and index updates run.

Existing swept-collision, ordnance and motion journeys protect first-contact behavior, circle/rotated-box geometry and continuation. Preserve integer rounding and tie order. See migration receipts for executed platforms; a broad-phase build is not a collision journey pass.
