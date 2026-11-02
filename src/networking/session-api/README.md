# Shared session interface

Start at `api.cppm` (`phoenix.session.api`).

Defines SessionPort, setup/role options and permitted session projections. It owns no authoritative World, network sockets or render state. Local and remote sessions implement this contract.

Allowed dependencies: Content definitions, portable contracts, transport options and inference connection contracts; never simulation or host factories.

Ownership and lifetime: Callers retain session ownership across pending commands and immutable observations. Session replacement invalidates route/claim state in its consumers.

`PresentationPreparation` owns explicit content-loading work, companion acquisition and selection replacement. It shares immutable `PreparedPresentation` handles through `NetworkOptions`; replacing a selection discards its pending download rather than publishing an obsolete completion. A same-content generation may retain its completed result. This loader does not own simulation ticks or Controller continuation.

Relevant validation: `system.local-session`, `system.input-and-semantics`, network client/GM service cases. These identify checks to run; this README is not execution evidence.
