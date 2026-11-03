# Lobby and admission

Start at `lobby_protocol.cppm` for shared participants; `lobby.cppm` for host composition.

Coordinates roles, claims, ready state and launch through injected LobbyServices. Shared protocol and authoritative host composition are distinct targets.

A pending lobby exposes its packaged public projection before it has authoritative content. Attaching a verified receipt checks the root identity and current claims on the owning session before enabling setup preparation.

Native fleet setup captures the verified content receipt, selection and roster for background preparation. The lobby installs only the result for its current revision and reuses the prepared World when the coordinator launches. Recovery bundles and browser preparation still use their existing synchronous paths.

Allowed dependencies: Shared protocol uses remote client/session/wire. Host composition additionally uses the host adapter and fleet setup.

Ownership and lifetime: Lobby state belongs to its session. A host owns launch/admission decisions; join parameters are hints rather than authority.

Relevant validation: `system.lobby-worker-consoles` and native lobby/network service cases. These identify checks to run; this README is not execution evidence.
