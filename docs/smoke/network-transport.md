# Transport smoke record

17 October 2026, same-machine Windows/Chromium development environment. Real transport adapters and V3 signalling service, synthetic opaque probe packets, no gameplay or public service deployment.

- Standalone native WebRTC data channel connected and delivered its payload.
- Two native processes negotiated a direct WebRTC round trip through the signalling service.
- Browser-initiated native round trip passed over direct WebRTC; a separate forced-relay browser run passed through the application relay.
- Native-initiated browser round trips passed over both direct WebRTC and forced application relay. Browser diagnostics were empty after the direct journey.
- A native WSS relay round trip passed with an explicitly trusted local test CA. The negative run initially exposed upstream's unconditional Windows verification bypass. The pinned build patch was added; the repeated trusted round trip passed and the repeated untrusted run reported TLS failure and delivered no probe reply.
- The headless service/native integration gate now reproduces the trusted/untrusted WSS checks with a fresh ephemeral certificate, alongside room isolation, source-bound routing, duplicate identity refusal and public plaintext bind refusal. Six native system cases and WASM content/simulation/session/replication cases pass separately.

Detailed captures/logs are under ignored `build/`, including `native-adapter-direct.log`, `native-to-browser-direct.log`, `native-to-browser-relay.log`, `native-wss-relay.log`, `native-wss-refusal.log` and `service-tls-check.log`. These checks prove the exercised transport paths, not Internet NAT traversal, browser WSS against a publicly trusted service, game admission, fleet replication over real connections or a six-screen performance target. No browser certificate warning was bypassed and no trust store was modified.

## Shared-policy refactor recheck — 17 October 2026

After moving discovery, envelope handling and route selection into shared C++, all four browser/native probe journeys passed again: direct WebRTC and forced relay, each initiated from browser and native. The browser used installed headless Chrome with a fresh temporary profile; HTTP and signalling listeners used private ephemeral loopback ports. Browser diagnostics and native stderr were empty. The harness closed its browser, listeners and native processes. Evidence is under ignored `build/audit-transport/results.json` and the per-case records beside it. This recheck covers the browser callback bridge and native adapter; it does not extend the hardware, Internet or public-browser-WSS claims above.
