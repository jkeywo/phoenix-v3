# Browser sessions — 20 October 2026

Manually triggered investigation of concurrent browser clients and inactive-tab slowdown. Source baseline: `14bbe9c`, with the changes recorded alongside this receipt.

## Findings and changes

The V2 implementation at `packages/session/src/session-token.js` separates per-tab identity from the persistent first-tab identity. It was inspected as historical implementation evidence, without editing V2.

V3's pre-game lobby already assigns random peer identities. However, direct admission to a running server reads durable client credentials under a key derived from room, server, local profile and role. Two browser tabs using `default` could therefore load the same credentials. The launcher now leases each runtime profile with Web Locks, retains the selected namespace in session storage, preserves the original profile for the first tab and gives concurrent/duplicated tabs distinct namespaces. Reload retains the tab namespace when available; a later fresh first tab can still recover the original durable profile. Locks avoid treating a throttled tab as expired.

The WASM entry point previously polled networking and ticked the host only inside the animation callback. Session polling, persistence and fixed ticks now run on an acknowledged worker clock independently of rendering. There is at most one pending worker message. Fixed-tick elapsed time uses the monotonic clock rather than the last rendered frame duration, with the existing bounded catch-up policy. The performance overlay continues accounting for session work between rendered frames.

## Verification

- All three WASM roles compiled and linked in an isolated checkout.
- Thirteen launcher integration tests passed, including real Web Locks with concurrent tabs, a duplicated session-storage namespace, reload and subsequent first-tab recovery.
- The shipped clock worker was exercised through Node's message-port adapter: two independent session pumps progress without animation callbacks; a blocked consumer does not accumulate clock messages; stopping prevents subsequent callbacks.
- The native lobby integration passed against a private real signalling fixture in 36.7 seconds. Its added case connects two clients on one machine with the same default profile, verifies independent generated lobby identities and approves both together.
- Local browser smoke used one host and two player tabs at the same origin, default profiles, and a loopback signalling service. Both players were admitted to the same Cruiser Free Flight lobby, claimed Helm and Engineering respectively, completed readiness and reached their running consoles together. Helm telemetry advanced from observed tick 381 to 1005, Engineering displayed changing coolant/resource readouts, and the host reached tick 1020 with durable consensus ready.

The available in-app browser is not a substitute for a sustained hidden-tab test on desktop/mobile Chrome: its visibility/lifecycle is controlled by the tool, and the inspected document reported visible. No exact background frame-rate or immunity to OS suspension is claimed. Actual frozen/discarded pages and sleeping devices still rely on ordinary disconnect/recovery. Transient disconnected lobby labels appeared during the browser interaction/startup work, then recovered; the existing one-time Cruiser launch stall remains separate from these fixes.

An initial local origin contained stale cached menu-preview data and could not start; the successful smoke used a fresh local origin. No user browser data was cleared.
