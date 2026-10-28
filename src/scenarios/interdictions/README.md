# Interdiction encounter ownership

Start with [interdictions.cppm](interdictions.cppm). book.cpp owns encounter rosters, receipts, docking/participant policies and strict checkpoint continuation. orders.cpp owns native motion, combat/formation choice and departure intentions; advance.cpp owns the sequential state-tree driver and takeover/plea lifecycle. These scenario integration policies are independent of physical psychic simulation.

Native bindings resolve permitted contacts and execute the existing Controller, docking, FTL, conversation and ownership paths in their existing sequence. InterdictionNative contains ten logical encounter operations. Initial liveness/ownership/player-active/live-count observations stay frozen across admitted outputs, while physical docking and geometry are re-read afterward. Spawn and conversation message receipts commit immediately in output order. There is no mutable state borrow. The owner exposes no World, ECS handle or callback collection.

Legacy tree compatibility takes exact former-registry signatures produced by the content adapter only when a saved program differs. The Book neither recompiles content nor resets continuation timers.

Validation: Existing synthetic interdiction/controller/checkpoint cases and fresh deterministic continuation traces after guarded activation. No acceptance is claimed for these private provider drafts before standalone and integrated gates.
