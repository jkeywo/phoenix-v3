# Native World bindings

These files implement the authoritative World facade and its private Runtime.
They compile as ordinary `phoenix.simulation` implementation units, grouped by
the domain they connect. The `:bindings` private interface partition makes the
canonical Runtime available to those units; the public World interface does
not reexport it. Independently compiled domain modules receive typed actors,
snapshots and bounded operations instead of importing the Runtime.

The family owners live outside this folder. Effects owns execution state and
effect policies; control owns controller policy and protocols; gameplay owns
native equipment and crew rules; knowledge owns permitted projections. Native
bindings locate entities, supply authored definition references and permitted
facts, and install domain results in the explicit World phase order.

The first ordinary-method outline also preserves integration policies awaiting
further ownership extraction. Moving a method into this folder does not by
itself establish that it is a thin adapter. The migration inventory records its
original source and exact body, and the architecture audit identifies residual
policy. The verified pre-outline source is available at commit `201a868`; all
824 Runtime bodies, 29 World facade bodies and 130 ordered field/type
declarations were audited before retiring its 57 private headers. Read the
relevant family README when changing a domain rule.

World composition is deliberately split across three other places:

- `world/lifecycle` constructs the native instance and assembles entities from
  compiled content.
- `world/persistence` validates and hydrates a candidate World before the final
  replacement. Domain-specific continuation codecs remain with their owners.
- `world/schedule` contains the explicit tick sequence and owns the optional
  independently compiled profiler. Timing never enters saved or authoritative
  state.

Queries and native handles remain shorter lived than their Flecs world. Scoped
actors resolve native state when an operation needs it; they do not retain
temporary snapshots or component references across unrelated mutations. Phase
ordering, work/payment receipts, generation fences and the candidate restore
swap are part of the behavior preserved by this split.
