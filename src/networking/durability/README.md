# Consensus durability coordination

Start at `persistence.cppm` (`phoenix.persistence`).

DurableConsensus owns the consensus/journal handshake and recovery policy. Journal encoding, OS locking and IndexedDB are owned by platform/storage, not this module.

Allowed dependencies: Peer consensus and the platform peer-store contract. The public interface reexports that single declaration owner for existing consumers.

Ownership and lifetime: Poll before handling inputs and flush after advancing the actor. Only matching flushed generations become durable acknowledgements; host/fleet retain commit application policy.

Relevant validation: `system.peer-storage`, `system.peer-consensus`, fleet recovery and storage manual browser probe. These identify checks to run; this README is not execution evidence.
