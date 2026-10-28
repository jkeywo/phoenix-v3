# Boarding

`phoenix.gameplay.boarding.state` owns boarding encounter, assessment, custody and objective continuation state. Its serializer is independently compiled in `serialization.cpp`; saved field names and strict reads retain their existing semantics.

World adapters still handle located entities, authority and cross-domain ownership effects. They now compile in `src/world/bindings/gameplay_boarding.cpp`; the ordinary compilation split does not make every policy in that adapter an independent domain mechanism.

`phoenix.gameplay.boarding.runtime` independently compiles route progression, encounter assessment and casualty resolution. Assessment consumes immutable native facts; casualty operations borrow bounded team/personnel references and the shared random state. RNG draws, loss records, report invalidation and revision updates preserve their original order. Route facts are collected only when the next movement step needs a route.

The runtime is active in the native graph and has passed the existing native integration and paired save/restore checkpoint. This is not a claim that its browser gate has run. Located defenders, conversations, capture ownership, transport and physical care remain explicit World bindings.
