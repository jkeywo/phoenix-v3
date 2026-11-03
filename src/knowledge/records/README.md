# Knowledge records

Start with `knowledge.cppm` (`phoenix.knowledge`). `knowledge_definitions.cpp` compiles schemas, roles and immutable bindings; `knowledge.cpp` owns record retention, scoped projection, coordination delivery and continuation. Console/ship integration belongs to sibling modules rather than this generic store.

`phoenix_knowledge_records_definitions` links portable foundation only. `phoenix_knowledge_records` adds execution without importing World, application, UI or content compilation.

The runtime owns provenance, record identities, private AI memory, pending deliveries and status latches. Rebuild transient decoded-field caches on restore and preserve message/record ordering. Existing knowledge/Coordination and Controller cases cover legitimate publication, expiry, delivery and continuation.

Private AI evidence can refresh without repeating an officer notification. Delivery compares typed claims on the same sender, destination and evidence stream. The saved delivery retains that presentation decision while the evidence still reaches its scoped board and AI inbox.

`evidence.cpp` supplies portable stream identity, newest-first stream selection and provenance quality. Consumers filter recipients before selection and test validity afterward; schema decoding and domain tie-breaking stay with the consumer. The selection borrows no records and persists no state.

Trusted direct publishers use `latest_ship_record` to obtain an owned snapshot of their exact entity/schema/subject/source stream, including its latest withdrawal. It excludes explicit publication/feed copies. Presentation limits never control authoritative comparison or revision allocation.
