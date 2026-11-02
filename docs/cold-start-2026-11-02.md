# Packaged cold-start optimisation — 2 November 2026

Status: implementation validated. Three matched before/after cold-start captures are running.

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

## Verification

Synthetic coverage exercises valid/equivalent companions, missing/corrupt/stale content, source order, ship/background selection, altered definitions, additional private entries, changed image pixels, fallback compilation, shared preparation, invalidation and obsolete completions. Transfer tests preserve identity/order and receipt reuse. World tests compare verified and editable construction/restore and reject an invalid edited catalogue.

The full native suite passed **53/53**. The active-retention regression was demonstrated with sixteen synthetic source revisions, then passed after the weak-index fix; the affected content and session integrations also pass. The native gate's later service-copy step and both WASM builds encountered a full disk. Removing 16,875,011,175 bytes of rebuildable compiler outputs from this conversation's older optimisation builds preserved executable snapshots, archives, logs and reports, and allowed validation to resume.

All **12 service journeys** passed after recovery, including valid/missing/corrupt companion paths, reconnect, explicit operator replacement, GM admission, fleet recovery and private setup transfer. Every probe target was refreshed after the final weak-index fix and all twelve journeys passed again (`service-final-build.log`, `service-final.log`, zero skipped). `gate-native.log`, `gate-native-finish.log`, the focused retention logs and the formatter/ownership logs retain their separate receipts. The full native suite predates only the final weak-index retention fix; the affected native checks were rebuilt and rerun after it.

The exploratory five-client run with isolated caches passed: one host preparation, five client artifact verifications, no image encoding, fallback compilation or runtime catalogue recompilation, and no missing observations. It ran under build load and is not a timing comparison. Earlier exploratory captures shared client caches and are excluded from the final comparison.

The final ordinary and threaded WASM Release builds and full integration gates passed (`gate-ordinary-final.log`, `gate-threaded-final.log`, both exit zero). All **23 native/ordinary WASM** and **23 native/threaded WASM** continuation comparisons match (`compare-ordinary-final.log`, `compare-threaded-final.log`). These gates executed the final runtime C++ source, including the active-retention fix. The later short-profile CMake aggregate changes validation selection rather than production compilation or gameplay behavior.

### Headed browser evidence

A visible in-app browser ran the final ordinary WASM client against the frozen final native host on loopback, with a new profile. The client verified the packaged companion, displayed the real Console roster, claimed Helm through normal admission, and received its permitted projection. The All stop action and speed control were exercised; the admitted speed setting changed from 61 to zero while station control remained active. This establishes usable controls, not completion of all physical braking or a browser latency measurement. The [captured final Helm control](../build/cold-start-2026-11-02/headed-final-helm-zero.png), `headed-final-helm-ax.txt`, `headed-http.log` and `headed-final-host.log` retain the evidence. No browser-private runtime state was read to establish that result. The disposable host was then intentionally stopped through the diagnostic abort marker.

This is browser rendering and control evidence, separate from Node/WASM checks. It does not establish physical screen-reader, touch/controller, six-display or frame-rate acceptance. Native Chrome access waited for app approval and timed out; the visible in-app browser completed the check without that dependency.

Three matched before/after measurements are pending.

## Validation cadence follow-up

At the user's request, `bdf8c7e7` makes routine validation short and reserves full runs for explicit invocation. `scripts/check.ps1` runs the native core profile by default; `-Target web` selects web and `-Long` selects the full sequence. Push/PR CI uses short profiles; manual workflow dispatch can enable `long_checks`. Linux uses the same core roster and `--long` opt-in. A single CMake aggregate builds the selected targets without repeated MSBuild dependency walks. Short web checks retain Release linking without forcing a redundant configuration when that setting is already current.

The public default passed **19 native checks** (60.49 seconds of test execution, excluding compilation), production preflight and the launcher/tool checks. The selected WASM core checks and production preflight passed in the final threaded tree. Aggregate builds passed on native/WASM and the module-boundary check passed afterward. PowerShell parsing, Linux shell syntax and full repository formatting pass. The new Linux runtime profile and hosted workflow dispatch have not been executed here. These are separate receipts from the long gates explicitly requested for this startup delivery; a short pass does not stand in for those gates.
