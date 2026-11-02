# Packets and bounded content transfers

Start at `wire.cppm` (`phoenix.wire`).

Encodes authenticated packets, transfer envelopes and bounded pack/projection content. It does not choose transport routes or own admission state.

Allowed dependencies: Portable data and resolved content contracts; no graphics or World execution.

Ownership and lifetime: Transfer instances own their bounded assembly buffers and identities. Decoding validation precedes consumers applying data.

Relevant validation: `system.network-content`, native remote session service cases. These identify checks to run; this README is not execution evidence.

Live projection bodies use `cbor-v1` full snapshots or `cbor-patch-v1` updates against the recipient's last acknowledged complete snapshot. Unchanged descriptions, schemas and placement metadata are absent from routine deltas; metadata changes still propagate. The offer identifies the base sequence/hash, encoded-body hash and reconstructed-state hash. Patches support only add/remove/replace and preserve numeric CBOR representations. Choose a full snapshot whenever it is smaller or a patch exceeds nesting bounds.

Both encoded input and reconstructed CBOR are bounded to 4 MiB, with bounded-depth parsing before allocation from container declarations. Only complete verified objects publish. Small authenticated packet envelopes and base64 chunk transport remain unchanged; content/private setup archives and authored JSON are separate. Matching updated peers are required; legacy JSON projection bodies are refused explicitly.
