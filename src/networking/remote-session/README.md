# Remote session adapters

Start at `client.cppm` and `host.cppm`.

Client/GM and host implementations share permitted projection helpers but remain separately linked targets. The client cannot obtain authoritative factories or unrestricted World state.

Allowed dependencies: Client: shared session, blobs, transport and wire. Host: local execution, fleet setup and durability in addition to those contracts.

Ownership and lifetime: Admission and sequence/generation checks remain host-owned. Projection/transfer buffers live with the session and preserve bounded remote observations.

Relevant validation: Native client/GM service cases, `system.local-session`, content transfer cases. These identify checks to run; this README is not execution evidence.

An in-flight projection is immutable. A valid chunk request acknowledges its offer for the current poll, so the host may omit that poll's duplicate offer. Idle offer retries still recover lost acknowledgements; projection cadence, transfer bounds and content do not change. The manually invoked separate-process traffic harness is described in [the optimisation receipt](../../../docs/performance-optimisation-2026-11-02.md).
