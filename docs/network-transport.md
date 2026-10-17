# Transport and signalling

`phoenix_transport` is an opaque, bounded text-packet transport. Native uses statically linked libdatachannel 0.24.6, libjuice/usrsctp/plog at its pinned submodule revisions, and Mbed TLS 3.6.7. Parent Git commit IDs are immutable in `cmake/Networking.cmake`. Browser uses platform WebSocket and RTCPeerConnection APIs behind a C++ interface. Game packet definitions and admission belong in C++, above this layer.

Both adapters use the same private C++ `LinkPolicy` for discovery, initiator choice, bounded signalling envelopes and direct/relay selection. Browser JavaScript handles the asynchronous WebSocket/WebRTC APIs and passes raw events to C++; it does not duplicate that policy. Backpressure on an open direct channel refuses the send instead of switching routes and reordering packets.

Peers discover routes through a room on the V3 signalling service. The lexicographically earlier route identity initiates a reliable ordered data channel. Packets use direct WebRTC when open and the application relay otherwise; forced-relay configuration disables direct negotiation. Each route bounds message size to 64 KiB and outgoing buffering to 4 MiB; receive queues and direct peer counts are bounded. Browser/native callbacks feed application-loop queues. Signalling loss does not close an existing direct channel or decide game membership. Discovery arrivals/departures are hints, not committed presence changes or station authority.

The service under `service/` handles discovery, SDP/ICE forwarding and opaque application relay. It contains no simulation, station admission, peer votes, log or game roster. Source route identity is attached by the connection; application-level credentials/epochs must still authenticate a server or station before admitting its packet. A network/QR room code alone is not a capability. Game-session authentication/admission is still pending; the transport probe is not a playable remote client.

## Run locally

Build native/web normally. Install service dependencies with the pinned Emsdk Node's `npm.cmd ci --prefix service --ignore-scripts`, then run `node service/server.mjs`. The default listener is loopback port 9080, endpoint `/phoenix`. `PHOENIX_PORT`, `PHOENIX_BIND`, `PHOENIX_TLS_CERT`, `PHOENIX_TLS_KEY` and comma-separated `PHOENIX_ORIGINS` configure it. A non-loopback plaintext listener is refused. WSS uses supplied certificate/key files. No service has been deployed to a public host.

The normal development route is `ws://127.0.0.1:9080/phoenix`. Public routes require WSS. URL validation rejects userinfo, fragments and plaintext non-loopback hosts. Native WSS loads Windows trusted root certificates or an explicitly configured CA file; browser WSS uses browser trust. Verification is never disabled by the V3 adapter.

The pinned upstream libdatachannel Windows code unconditionally disables WebSocket certificate verification. `cmake/PatchDatachannel.cmake` removes that bypass for our Mbed TLS backend, which receives an explicit CA bundle. The checked patch fails configuration if its expected source block changes. The integration gate creates an ephemeral test CA/certificate, checks a real native WSS relay round trip, then checks that the same certificate is refused without that CA. The test changes no OS/browser trust settings. Git for Windows' OpenSSL executable supplies this synthetic certificate fixture.

## Manual connection probes

```powershell
./build/native/Release/phoenix_network_probe.exe --echo --room synthetic-network --seconds 120
```

Serve `build/web` and open `network-probe.html?room=synthetic-network&self=browser&remote=native`. The C++ probe waits for a direct round trip. Add `relay=1` to require the application relay. It exposes a result/status only; it is separate from the four user application entry points. Native can initiate the same probe with `--self`, `--remote`, `--url`, `--room`, `--relay` and optional `--ca`. `--echo` starts a responder. These probes are manually invoked smoke evidence, not mandatory timing/performance assertions.

See [transport smoke evidence](smoke/network-transport.md). Game/session wire formats, credentials and scoped projections; roster setup/readiness; bounded pack transfers/cache; integration with durable consensus; committed absence/station changes; QR; and actual mixed-platform fleet journeys remain to be implemented.
