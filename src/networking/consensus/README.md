# Peer consensus actor

Start at `replication.cppm` (`phoenix.replication`).

Owns transport-independent peer consensus, stable term/index commitments and durable transaction validation. It has no gameplay, graphics or OS storage implementation.

Allowed dependencies: Portable data/JSON only. Durability and fleet modules drive this actor.

Ownership and lifetime: Drive one actor serially; acknowledge persistence by generation. Restored hard state cannot rewrite a committed prefix.

Relevant validation: `system.peer-consensus`, fleet recovery service cases and native/WASM continuation. These identify checks to run; this README is not execution evidence.
