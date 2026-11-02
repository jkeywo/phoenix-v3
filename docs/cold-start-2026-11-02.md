# Packaged cold-start optimisation — 2 November 2026

Status: implemented, verified and measured. All cold-start acceptance checks below passed.

## Scope

The journey starts a fresh native host, prepares `base.rescue-under-threat` with `base.cruiser` in the `courier` slot, then admits five separate direct crew clients. Each Console must retain usable observations for the diagnostic's existing five-second readiness gate. Host preparation, first-client readiness and all-five readiness are measured from host spawn. Exact first-projection milestones are additionally available in the new executable.

Every capture uses distinct empty application storage and an independent fresh cache for each client, enforced by `--cold`. The harness mirrors capture markers into each client's directory, including when running the unchanged baseline executable. Filesystem caches are not flushed: this is application-cold startup, not a disk-cache-cold benchmark. The archive and packaged Cruiser companion are frozen across both revisions. Final timing captures run with local builds and other diagnostics idle.

## Implementation

- Production validation creates an opaque immutable `VerifiedPacks` receipt containing ordered archives and their compiled catalogue. Exact ordered hashes can reuse a live receipt. Transfer, local session composition and World construction/restore consume it without re-resolving definitions. Editable catalogues and ordinary archive inputs retain production validation.
- Presentation loading constructs the permitted draft. A companion must match the manifest, complete entry roster, canonical encoded definitions, exact non-image bytes and decoded image dimensions/pixels. Canonical comparison also rejects ignored duplicate encoded fields. Neither a filename nor a sidecar hash establishes permission. Missing, corrupt or stale companions compile the current draft once during preparation.
- Prepared keys cover source order, ship, effective draft/background content and compiler version. Lobby and runtime hosting share immutable preparation handles. A weak index preserves discoverability of active handles after bounded cache eviction, without retaining unused bundles. Actual controllable entity definitions determine presentation selection, including scenario instance differences. Obsolete acquisition jobs cannot publish into a replacement selection.
- Claims and projections select prepared sources. The full own-ship bundle is offered before the first claim, removing the previous selection-bundle-to-full-bundle transition. Existing claim, knowledge, GM approval, replacement and transfer bounds remain in force.
- Native configured source locations and browser downloads acquire installed companions. Private locations are not published to peers. Ordinary remote downloads remain HTTP(S)-only. Raw imports and Workshop edits retain the compilation fallback.
- A fixed opaque-white contact marker uses its existing compiled bytes, avoiding encoding during draft construction. Detailed hull art that the Console projection discards is no longer copied into that intermediate draft.

World still owns authoritative construction, candidate recovery and scheduling. Only immutable definitions and archives are shared. Console observations, blackboards, inboxes, Controller continuation and physical runtime state are not pooled.

## Diagnostics and reproduction

`PHOENIX_PROFILE_STARTUP=1` enables stage timings/work counters; absent, scopes do not sample the clock. Archive IO, catalogue resolution/compilation, World construction, companion validation/compilation, actual image encodes, client verification, claims and first projection are reported without content bodies. Timings are nested and must not be summed as independent work.

```powershell
.tools/emsdk/node/24.19.0_64bit/node.exe tests/manual/network_profile.mjs `
  --multiprocess --direct --staggered --cold --startup-profile `
  <frozen-probe.exe> build/cold-start-2026-11-02/content/base.zip `
  base.rescue-under-threat courier base.cruiser 5 30 <unused-output-directory> crew
```

The harness records host-relative milestones, executable identity, admission/observation gaps, signalling and direct transport bytes. Installed companion reads are distinct from network transfer. Counters exclude ICE, DTLS/SCTP, TCP/IP and WebSocket framing. The diagnostic requests a tick 50 ms after the previous request returns; these runs establish neither 20 Hz simulation nor projection capacity.

Raw evidence is retained under ignored `build/cold-start-2026-11-02/`. The pre-change executable is frozen from `79ae22d071eed685af78f3b58173baab0eceb24f`; subsequent unrelated commits tidy diagnostics, content formatting and generated inventories. The frozen archive prevents those content formatting changes entering this comparison.

| Frozen input                        | SHA-256                                                            |
| ----------------------------------- | ------------------------------------------------------------------ |
| Baseline executable                 | `249c01c89ea4a43a45279df014dd4128b65895ef74ad321d4650fb9205041ce9` |
| Final executable                    | `a8d9716fe4a36aa1d75b947b4fe9a4a0140fcbeb4b7c558322bfc2717499289f` |
| Gameplay archive, 120,812,731 bytes | `66e3fb59397cca56503ea2ef6881d0c049f6747c2dd52f96863985338777c146` |
| Cruiser companion, 5,129,349 bytes  | `256c16eeca399c1cb708809c0d506a64d23bffeaf68496d874843c4a57bfce13` |

## Matched cold-start measurements

Three alternating before/after pairs ran with this task's builds and validation idle. Unrelated workspace editing resumed late in the measurement window; external CPU load was not continuously recorded. Frozen executables, archives and companions prevented source/content mixing, and the per-run ranges retain the observed variation. The machine was Windows on an Intel Core Ultra 9 275HX (24 cores/logical processors), with approximately 63.4 GiB of OS-visible RAM. Runtime implementation: `d8e6091a`; normalized C++ source fingerprint: `e004c057dd624256450db02d9c5d017c621909db0b0e8f57dfbad09864855fab`. The frozen executable hashes above define the actual binaries measured.

The common endpoints are host-process spawn to host readiness, first Console readiness and all-five readiness. Clients are deliberately staggered; each readiness announcement follows five continuous seconds of usable permitted observations. These are production preparation/admission diagnostic timings, rather than native-window rendering or browser asset-download timings. The separate headed check establishes real browser controls.

| Metric                                      | Before mean (range)                      | After mean (range)                       | Reduction |
| ------------------------------------------- | ---------------------------------------- | ---------------------------------------- | --------- |
| Host preparation                            | 60.12 s (59.82–60.31)                    | 30.46 s (29.45–32.21)                    | 49.3%     |
| First Console ready                         | 286.48 s (284.32–287.85)                 | 38.36 s (37.66–39.53)                    | 86.6%     |
| All five ready / total journey              | 372.85 s (369.02–375.67)                 | 73.00 s (71.55–75.35)                    | 80.4%     |
| Host-ready to all-five-ready                | 312.73 s (309.19–315.37)                 | 42.54 s (42.10–43.15)                    | 86.4%     |
| Startup application bytes accepted for send | 86,683,259 bytes (85,717,689–88,012,767) | 21,390,126 bytes (21,266,722–21,547,570) | 75.3%     |

| Variant | Run | Host (s) | First (s) | All five (s) | Admission (s) | Startup send (bytes) | Maximum projection gap (s) |
| ------- | --- | -------- | --------- | ------------ | ------------- | -------------------- | -------------------------- |
| before  | 1   | 59.82    | 284.32    | 369.02       | 309.19        | 86,319,321           | 1.092                      |
| before  | 2   | 60.22    | 287.85    | 373.85       | 313.64        | 88,012,767           | 0.930                      |
| before  | 3   | 60.31    | 287.27    | 375.67       | 315.37        | 85,717,689           | 1.226                      |
| after   | 1   | 29.73    | 37.88     | 72.11        | 42.38         | 21,266,722           | 0.936                      |
| after   | 2   | 29.45    | 37.66     | 71.55        | 42.10         | 21,356,086           | 0.935                      |
| after   | 3   | 32.21    | 39.53     | 75.35        | 43.15         | 21,547,570           | 0.926                      |

All six captures exited successfully, retained all six native transport reports, and admitted five clients. No Console had a missing-observation poll during the subsequent thirty-second capture. Projection gaps are observations of these paced runs, not a capacity or latency guarantee.

Admission spacing is shown below as host-ready → Console 1 → Console 2 → Console 3 → Console 4 → Console 5. It includes the same deliberate five-second stable-readiness gate at each step; it is not network round-trip time.

| Variant | Run | Successive readiness intervals (s)     |
| ------- | --- | -------------------------------------- |
| before  | 1   | 224.49 / 22.08 / 18.48 / 20.77 / 23.36 |
| before  | 2   | 227.64 / 22.32 / 19.14 / 20.46 / 24.08 |
| before  | 3   | 226.97 / 22.39 / 19.62 / 21.54 / 24.85 |
| after   | 1   | 8.15 / 7.36 / 8.58 / 8.29 / 10.00      |
| after   | 2   | 8.21 / 7.29 / 8.57 / 8.16 / 9.87       |
| after   | 3   | 7.32 / 7.49 / 8.89 / 8.30 / 11.15      |

Every after run recorded **one shared host presentation preparation, five client artifact verifications, zero WebP/PNG encodes, zero compiler fallbacks and zero runtime catalogue re-preparations**. No host `presentation.prepare`, `archive.write`, `catalog.resolve`, `catalog.prepare_runtime` or image-encoding event occurred after host readiness. The seven required catalogue compilations are one host gameplay catalogue, one host presentation catalogue and one presentation catalogue in each of five separate clients; they are not seven gameplay recompilations. The baseline executable predates opt-in work instrumentation, so its work-counter values are unavailable, rather than measured zeros.

Startup send bytes sum each native sender once across direct and relay application routes, through the all-five-ready marker. They include normal admission, retries and initial/running projections. They exclude installed-file reads, HTTP bodies and transport framing; accepted-send counters are not proof of every individual packet being delivered. The thirty-second post-ready capture is excluded from these startup totals.

The native fast path reads the existing 5,129,349-byte companion locally on the host and five clients (30,776,094 file bytes across six processes). Browser clients still need their companion over HTTP: five fresh clients would fetch 25,646,745 companion-body bytes, plus application assets and ordinary session traffic. This optimization reuses packaged content; it does not make content distribution free.

Raw captures are `matched-before-1` through `matched-before-3` and `matched-after-1` through `matched-after-3`; `matched-summary.json` contains the reduced measurements and work checks. Earlier shared-cache explorations and captures taken under build load are excluded.

## Remaining startup costs

| After host stage                                         | Mean measured time |
| -------------------------------------------------------- | ------------------ |
| Authoritative World construction                         | 16.883 s           |
| Catalogue resolution (gameplay plus presentation)        | 11.018 s           |
| Archive decoding/hash reads                              | 1.306 s            |
| Presentation preparation (includes companion validation) | 0.740 s            |
| Companion validation (nested within preparation)         | 0.236 s            |

Stage timings are nested and must not be summed. The remaining dominant costs are the first full catalogue compilation and physical World construction. The diagnostic also parses the base archive to obtain configuration before the host loads it, and each native client parses that archive before acquiring its presentation. Reusing those initial archive/configuration results is further work; no validation is bypassed here. Raw imports, changed Workshop content and invalid companions retain the potentially expensive explicit compiler fallback before their seats become ready.

The headed fixture served 50,902,230 bytes of `client.data`, 17,581,727 bytes of `client.wasm`, 520,778 bytes of `client.js` and the 5,129,349-byte companion, in addition to menu/font resources. These are local uncompressed HTTP body sizes, separate from the native-peer table. Browser bundle/download cost and graphics/window initialization still need their own matched headed latency measurements. This receipt establishes lower measured preparation/admission time and working headed controls, not a six-display/FPS result or a 20 Hz simulation capacity claim.

## Verification

Synthetic coverage exercises valid/equivalent companions, missing/corrupt/stale content, source order, ship/background selection, altered definitions, additional private entries, changed image pixels, fallback compilation, shared preparation, invalidation and obsolete completions. Transfer tests preserve identity/order and receipt reuse. World tests compare verified and editable construction/restore and reject an invalid edited catalogue.

The full native suite passed **53/53**. The active-retention regression was demonstrated with sixteen synthetic source revisions, then passed after the weak-index fix; the affected content and session integrations also pass. The native gate's later service-copy step and both WASM builds encountered a full disk. Removing 16,875,011,175 bytes of rebuildable compiler outputs from this conversation's older optimisation builds preserved executable snapshots, archives, logs and reports, and allowed validation to resume.

All **12 service journeys** passed after recovery, including valid/missing/corrupt companion paths, reconnect, explicit operator replacement, GM admission, fleet recovery and private setup transfer. Every probe target was refreshed after the final weak-index fix and all twelve journeys passed again (`service-final-build.log`, `service-final.log`, zero skipped). `gate-native.log`, `gate-native-finish.log`, the focused retention logs and the formatter/ownership logs retain their separate receipts. The full native suite predates only the final weak-index retention fix; the affected native checks were rebuilt and rerun after it.

The exploratory five-client run with isolated caches passed: one host preparation, five client artifact verifications, no image encoding, fallback compilation or runtime catalogue recompilation, and no missing observations. It ran under build load and is not a timing comparison. Earlier exploratory captures shared client caches and are excluded from the final comparison.

The final ordinary and threaded WASM Release builds and full integration gates passed (`gate-ordinary-final.log`, `gate-threaded-final.log`, both exit zero). All **23 native/ordinary WASM** and **23 native/threaded WASM** continuation comparisons match (`compare-ordinary-final.log`, `compare-threaded-final.log`). These gates executed the runtime C++ source recorded in `d8e6091a`, including the active-retention fix. The later short-profile CMake aggregate changes validation selection rather than production compilation or gameplay behavior. Subsequent unrelated workspace candidate/tuning edits are preserved and are outside these receipts.

### Headed browser evidence

A visible in-app browser ran the final ordinary WASM client against the frozen final native host on loopback, with a new profile. The client verified the packaged companion, displayed the real Console roster, claimed Helm through normal admission, and received its permitted projection. The All stop action and speed control were exercised; the admitted speed setting changed from 61 to zero while station control remained active. This establishes usable controls, not completion of all physical braking or a browser latency measurement. The [captured final Helm control](../build/cold-start-2026-11-02/headed-final-helm-zero.png), `headed-final-helm-ax.txt`, `headed-http.log` and `headed-final-host.log` retain the evidence. No browser-private runtime state was read to establish that result. The disposable host was then intentionally stopped through the diagnostic abort marker.

This is browser rendering and control evidence, separate from Node/WASM checks. It does not establish physical screen-reader, touch/controller, six-display or frame-rate acceptance. Native Chrome access waited for app approval and timed out; the visible in-app browser completed the check without that dependency.

The matched captures above complete the requested timing comparison.

## Validation cadence follow-up

At the user's request, `bdf8c7e7` makes routine validation short and reserves full runs for explicit invocation. `scripts/check.ps1` runs the native core profile by default; `-Target web` selects web and `-Long` selects the full sequence. Push/PR CI uses short profiles; manual workflow dispatch can enable `long_checks`. Linux uses the same core roster and `--long` opt-in. A single CMake aggregate builds the selected targets without repeated MSBuild dependency walks. Short web checks retain Release linking without forcing a redundant configuration when that setting is already current.

The public default passed **19 native checks** (60.49 seconds of test execution, excluding compilation), production preflight and the launcher/tool checks. The selected WASM core checks and production preflight passed in the final threaded tree. Aggregate builds passed on native/WASM and the module-boundary check passed afterward. PowerShell parsing, Linux shell syntax and full repository formatting pass. The new Linux runtime profile and hosted workflow dispatch have not been executed here. These are separate receipts from the long gates explicitly requested for this startup delivery; a short pass does not stand in for those gates.
