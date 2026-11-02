# Peer transport adapters

Start at `transport.cppm` (`phoenix.transport`).

Owns socket/WebRTC adapters, bounded public-content downloads and shared discovery/routing policy. LinkPolicy and envelope helpers are private to this named module; native/browser backends implement the same public interface.

Allowed dependencies: Portable data; native libdatachannel and platform TLS/HTTP libraries, or browser lifecycle/fetch services.

Ownership and lifetime: One transport instance owns its channels/events and bounded queues. Accepted send does not imply delivered/admitted/committed. Preserve direct-channel backpressure and signalling-loss behaviour.

`open_content_artifact` is the trusted composition adapter for bounded packaged-companion reads: native files or HTTP(S), and browser relative/same-origin downloads or HTTP(S). Ordinary remote `download_content` remains HTTP(S)-only. Replacing the owning preparation drops its result handle; an outstanding native read can finish without publishing into the new selection.

Relevant validation: Native verified WSS/relay service cases and manual native/browser interoperability probe. These identify checks to run; this README is not execution evidence.

The optional native `PHOENIX_TRANSPORT_PROFILE_DIRECTORY` diagnostic counts application message sizes by direct/relay route and startup/measured/teardown phase, using profiler-only `measure.start` and `measure.stop` markers. It records accepted sends, received packets, rejected sends and receive overflow polls without bodies. Reports are written on transport destruction and exclude transport framing. Unset, it adds no counter allocation or filesystem sampling. It does not change routing, admission or persistent state.

Small typed state offers also provide encoded transfer-size samples per recipient and route. For binary projection updates this is the CBOR full/delta size, not the reconstructed JSON size. Stream/sequence values are retained only as private deduplication keys and are omitted from reports. The diagnostic never parses projection chunks.
