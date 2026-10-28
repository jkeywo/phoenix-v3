# Remote session adapters

Start at `client.cppm` and `host.cppm`.

Client/GM and host implementations share permitted projection helpers but remain separately linked targets. The client cannot obtain authoritative factories or unrestricted World state.

Allowed dependencies: Client: shared session, blobs, transport and wire. Host: local execution, fleet setup and durability in addition to those contracts.

Ownership and lifetime: Admission and sequence/generation checks remain host-owned. Projection/transfer buffers live with the session and preserve bounded remote observations.

Relevant validation: Native client/GM service cases, `system.local-session`, content transfer cases. These identify checks to run; this README is not execution evidence.
