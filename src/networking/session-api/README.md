# Shared session interface

Start at `api.cppm` (`phoenix.session.api`).

Defines SessionPort, setup/role options and permitted session projections. It owns no authoritative World, network sockets or render state. Local and remote sessions implement this contract.

Allowed dependencies: Content definitions, portable contracts, transport options and inference connection contracts; never simulation or host factories.

Ownership and lifetime: Callers retain session ownership across pending commands and immutable observations. Session replacement invalidates route/claim state in its consumers.

Relevant validation: `system.local-session`, `system.input-and-semantics`, network client/GM service cases. These identify checks to run; this README is not execution evidence.
