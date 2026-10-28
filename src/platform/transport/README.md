# Peer transport adapters

Start at `transport.cppm` (`phoenix.transport`).

Owns socket/WebRTC adapters, bounded public-content downloads and shared discovery/routing policy. LinkPolicy and envelope helpers are private to this named module; native/browser backends implement the same public interface.

Allowed dependencies: Portable data; native libdatachannel and platform TLS/HTTP libraries, or browser lifecycle/fetch services.

Ownership and lifetime: One transport instance owns its channels/events and bounded queues. Accepted send does not imply delivered/admitted/committed. Preserve direct-channel backpressure and signalling-loss behaviour.

Relevant validation: Native verified WSS/relay service cases and manual native/browser interoperability probe. These identify checks to run; this README is not execution evidence.
