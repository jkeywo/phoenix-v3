# Swept ordnance contact ordering

Start with [contacts.cppm](contacts.cppm).

Owns body/missile broad-phase indexes, swept contact/fuse geometry and deterministic time/type/id ordering for one fixed tick. It captures native geometry, not authoritative participants or ECS handles. The native adapter filters current liveness for the complete returned roster before processing any contact, preserving within-hit death behavior.

Destruction/reaction/penetration effects remain at their typed native boundaries. No callback table or universal World context is exposed.

Validation: Existing synthetic native combat/controller/continuation cases through the production World adapter after activation. These sources are prepared privately until that integration gate.
