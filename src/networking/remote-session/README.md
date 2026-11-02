# Remote session adapters

Start at `client.cppm` and `host.cppm`.

Client/GM and host implementations share permitted projection helpers but remain separately linked targets. The client cannot obtain authoritative factories or unrestricted World state.

Allowed dependencies: Client: shared session, blobs, transport and wire. Host: local execution, fleet setup and durability in addition to those contracts.

Ownership and lifetime: Admission and sequence/generation checks remain host-owned. Projection/transfer buffers live with the session and preserve bounded remote observations.

Host loading prepares presentation sources for the actual controllable entity definitions before making their seats available. Claim and projection paths select these immutable sources; they do not compile assets or resolve catalogues. Lobby/runtime composition carries the presentation cache and verified pack receipts, including received client bundles. No Console observations, blackboards, inboxes or private AI continuation are shared by these caches.

Relevant validation: Native client/GM service cases, `system.local-session`, content transfer cases. These identify checks to run; this README is not execution evidence.

An in-flight projection is immutable. A valid chunk request acknowledges its offer for the current poll, so the host may omit that poll's duplicate offer. Idle offer retries still recover lost acknowledgements; projection cadence, transfer bounds and content do not change. The manually invoked separate-process traffic harness is described in [the optimisation receipt](../../../docs/performance-optimisation-2026-11-02.md).

Each client retains its own acknowledged binary projection baseline. The host advances that baseline only on the matching stream/sequence acknowledgement. The receiver reconstructs the entire current scoped model before normal observation validation; removals withdraw old fields rather than retaining them as metadata. Content/credential/stream changes clear caches. A missing or mismatched baseline requests `state-reset` for that exact in-flight sequence, forcing a full snapshot without weakening claims, leases or scope.
