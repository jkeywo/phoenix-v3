# Peer transport adapters

Start at `transport.cppm` (`phoenix.transport`).

Owns socket/WebRTC adapters, bounded public-content downloads and shared discovery/routing policy. LinkPolicy and envelope helpers are private to this named module; native/browser backends implement the same public interface.

Allowed dependencies: Portable data; native libdatachannel and platform TLS/HTTP libraries, or browser lifecycle/fetch services.

Ownership and lifetime: One transport instance owns its channels/events and bounded queues. Accepted send does not imply delivered/admitted/committed. Preserve direct-channel backpressure and signalling-loss behaviour.

Relevant validation: Native verified WSS/relay service cases and manual native/browser interoperability probe. These identify checks to run; this README is not execution evidence.

The optional native `PHOENIX_TRANSPORT_PROFILE_DIRECTORY` diagnostic counts application message sizes by direct/relay route and startup/measured/teardown phase, using profiler-only `measure.start` and `measure.stop` markers. It records accepted sends, received packets, rejected sends and receive overflow polls without bodies. Reports are written on transport destruction and exclude transport framing. Unset, it adds no counter allocation or filesystem sampling. It does not change routing, admission or persistent state.

Small typed state offers also provide raw snapshot-size samples per recipient and route. Stream/sequence values are retained only as private deduplication keys and are omitted from reports. The diagnostic never parses projection chunks.
