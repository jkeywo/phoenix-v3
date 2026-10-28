# Lobby and admission

Start at `lobby_protocol.cppm` for shared participants; `lobby.cppm` for host composition.

Coordinates roles, claims, ready state and launch through injected LobbyServices. Shared protocol and authoritative host composition are distinct targets.

Allowed dependencies: Shared protocol uses remote client/session/wire. Host composition additionally uses the host adapter and fleet setup.

Ownership and lifetime: Lobby state belongs to its session. A host owns launch/admission decisions; join parameters are hints rather than authority.

Relevant validation: `system.lobby-worker-consoles` and native lobby/network service cases. These identify checks to run; this README is not execution evidence.
