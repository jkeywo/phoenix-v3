# World checkpoints

`phoenix.world.persistence` owns checkpoint envelope, allocator, roster and numeric body validation. Its inputs are immutable saved data and explicit expected content or entity identities; it cannot query or mutate a World. `CheckpointRoster` owns only the identities already accepted during a candidate restore.

World composes restoration into a separate candidate, invokes each check at its original point, restores domain continuations in their existing order and swaps the candidate into the live session only after all validation succeeds. Schema 32 and pack identity remain unchanged. Relevant integration cases are malformed saves, continuation, restore rejection and deterministic native/browser checkpoints.

World calls these checks at their original restore points; domain continuation validation remains with each native owner.
