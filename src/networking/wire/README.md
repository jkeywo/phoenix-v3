# Packets and bounded content transfers

Start at `wire.cppm` (`phoenix.wire`).

Encodes authenticated packets, transfer envelopes and bounded pack/projection content. It does not choose transport routes or own admission state.

Allowed dependencies: Portable data and resolved content contracts; no graphics or World execution.

Ownership and lifetime: Transfer instances own their bounded assembly buffers and identities. Decoding validation precedes consumers applying data.

Relevant validation: `system.network-content`, native remote session service cases. These identify checks to run; this README is not execution evidence.
