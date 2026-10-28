# Networking family

Read session-api for the consumer contract, then local-session or remote-session for its adapters. Lobby coordinates admission and launch. Wire and platform transport carry bounded messages; they do not grant authority.

Consensus is the transport/gameplay-independent actor. Durability drives its persistence handshake through platform/storage. Fleet composes the fixed-roster driver; fleet setup remains a distinct authoritative target. Inference connections carry external decision proposals, with Controller policy above them.

Each child owns its named interfaces and local CMake targets. Client/GM/shared lobby targets must never acquire local execution, host/fleet setup or simulation transitively. Module READMEs identify state lifetimes and applicable production-path checks.
