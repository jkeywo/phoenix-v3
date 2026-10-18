# Transport and signalling

`phoenix_transport` is an opaque, bounded text-packet transport. Native uses statically linked libdatachannel 0.24.6, libjuice/usrsctp/plog at its pinned submodule revisions, and Mbed TLS 3.6.7. Parent Git commit IDs are immutable in `cmake/Networking.cmake`. Browser uses platform WebSocket and RTCPeerConnection APIs behind a C++ interface. Game packet definitions and admission belong in C++, above this layer.

Both adapters use the same private C++ `LinkPolicy` for discovery, initiator choice, bounded signalling envelopes and direct/relay selection. Browser JavaScript handles the asynchronous WebSocket/WebRTC APIs and passes raw events to C++; it does not duplicate that policy. Backpressure on an open direct channel refuses the send instead of switching routes and reordering packets.

Peers discover routes through a room on the V3 signalling service. The lexicographically earlier route identity initiates a reliable ordered data channel. Packets use direct WebRTC when open and the application relay otherwise; forced-relay configuration disables direct negotiation. Each route bounds message size to 64 KiB and outgoing buffering to 4 MiB; receive queues and direct peer counts are bounded. Browser/native callbacks feed application-loop queues. Signalling loss does not close an existing direct channel or decide game membership. Discovery arrivals/departures are hints, not committed presence changes or station authority.

The service under `service/` handles discovery, SDP/ICE forwarding and opaque application relay. It contains no simulation, station admission, peer votes, log or game roster. Source route identity is attached by the connection; shared C++ application credentials/epochs authenticate a server or station before admitting its packet. A network/QR room code alone is not a capability. See [network play](network-play.md) for the implemented approval, content and Console boundary.

## Run locally

Build native/web normally. Install service dependencies with the pinned Emsdk Node's `npm.cmd ci --prefix service --ignore-scripts`, then run `node service/server.mjs`. The default listener is loopback port 9080, endpoint `/phoenix`. `PHOENIX_PORT`, `PHOENIX_BIND`, `PHOENIX_TLS_CERT`, `PHOENIX_TLS_KEY` and comma-separated `PHOENIX_ORIGINS` configure it. A non-loopback plaintext listener is refused unless explicitly configured as a private backend behind a TLS proxy (see below). WSS uses supplied certificate/key files. No service has been deployed to a public host.

The normal development route is `ws://127.0.0.1:9080/phoenix`. Public routes require WSS. URL validation rejects userinfo, fragments and plaintext non-loopback hosts. Native WSS loads Windows trusted root certificates or an explicitly configured CA file; browser WSS uses browser trust. Verification is never disabled by the V3 adapter.

The pinned upstream libdatachannel Windows code unconditionally disables WebSocket certificate verification. `cmake/PatchDatachannel.cmake` removes that bypass for our Mbed TLS backend, which receives an explicit CA bundle. The checked patch fails configuration if its expected source block changes. The integration gate creates an ephemeral test CA/certificate, checks a real native WSS relay round trip, then checks that the same certificate is refused without that CA. The test changes no OS/browser trust settings. Git for Windows' OpenSSL executable supplies this synthetic certificate fixture.

## Northflank deployment

Use one free Sandbox **combined service**, sourced from `jkeywo/phoenix-v3`, branch `main`. Select **Buildpack (Heroku)** with build context `/service`; the existing `npm start` script runs the Node server. Keep one instance: room/discovery state is in memory and is not shared across replicas. Enable CI/CD for automatic service builds and deployments.

Expose port **8080**, protocol **HTTP**, accessibility **Public**. Northflank terminates HTTPS/WSS at its managed ingress and forwards HTTP to this backend. Set these **runtime variables**:

| Variable            | Value                      |
| ------------------- | -------------------------- |
| `PHOENIX_BIND`      | `0.0.0.0`                  |
| `PHOENIX_PORT`      | `8080`                     |
| `PHOENIX_TLS_PROXY` | `1`                        |
| `PHOENIX_ORIGINS`   | `https://jkeywo.github.io` |

Use an HTTP health check on port 8080, path `/health`, which returns `Phoenix V3 signalling`. Once deployed, use the generated public hostname as `wss://<hostname>/phoenix` in the game client's Network service setting. Visiting the hostname's root returns 404 by design; `/health` is the browser-readable check. No custom domain, database, certificate file or certificate secret is needed. Additional allowed browser origins can be comma-separated; native clients need no Origin header. Origin checks are not game authentication.

`PHOENIX_TLS_PROXY=1` permits an unencrypted backend listener only when an operator deliberately configures a trusted ingress. Keep that backend reachable only through the provider's ingress/private network; never expose it directly as public plaintext. The service does not trust `X-Forwarded-*` headers. The 32-connection per-address cap consequently applies to each ingress address, not each original client; this is adequate for a small test group but needs revisiting before larger deployments. Browser/native clients continue requiring public WSS and checking certificates. Without this opt-in, public plaintext remains refused.

`PHOENIX_PORT` takes precedence over the platform's `PORT`, then defaults to 9080. SIGTERM/SIGINT close active sockets and stop the service. Deployments/restarts discard discovery rooms and close relay connections; test reconnection before relying on a public play session. The Node integration case launches the actual service entry point in proxy mode and verifies health, browser-origin admission, native discovery and relay. It does not establish Northflank ingress or public-network acceptance; those require a live deployment check.

## Manual connection probes

```powershell
./build/native/Release/phoenix_network_probe.exe --echo --room synthetic-network --seconds 120
```

Serve `build/web` and open `network-probe.html?room=synthetic-network&self=browser&remote=native`. The C++ probe waits for a direct round trip. Add `relay=1` to require the application relay. It exposes a result/status only; it is separate from the four user application entry points. Native can initiate the same probe with `--self`, `--remote`, `--url`, `--room`, `--relay` and optional `--ca`. `--echo` starts a responder. These probes are manually invoked smoke evidence, not mandatory timing/performance assertions.

See [transport smoke evidence](smoke/network-transport.md), [network journeys](smoke/network-play.md) and [current MVP status](mvp-status.md). Browser launcher `relay=1` and native `--relay` force application relay for controlled connectivity checks. Off-machine connectivity and public service deployment remain separate from same-machine acceptance.
