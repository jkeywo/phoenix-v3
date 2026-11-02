# Binary live projections and acknowledged updates

The 2 November follow-up replaces textual JSON **live projection bodies** with binary CBOR and removes unchanged metadata/values from routine transfers. This follows the user's agreement to start by removing JSON and repeated stable metadata. Internal permitted read models, authored content and saves keep their existing representation; small authenticated control envelopes and base64 chunk carriage remain unchanged.

## Transfer contract

The first projection is a complete `cbor-v1` snapshot. Subsequent `cbor-patch-v1` bodies contain only add/remove/replace operations against that recipient's last acknowledged snapshot. Routine deltas omit unchanged descriptions, action/input schemas, command bounds and placement maps; full recovery/fallback snapshots still include them. They are not assumed immutable: edits propagate through the same update mechanism.

The host retains one acknowledged baseline and one immutable in-flight projection per recipient. It advances the baseline only on the matching stream/sequence acknowledgement. The offer identifies the base sequence and complete-state digest, the transfer-body digest and the reconstructed-state digest. The client reconstructs the complete current model before ordinary observation/permission validation. Explicit removals withdraw old fields; cached metadata never supplies missing evidence. No board, inbox, private continuation or knowledge scope is merged.

A missing or mismatched baseline requests `state-reset` for that exact in-flight sequence. The next transfer is full. Content, credential and stream changes clear caches. Lost final acknowledgements retain the existing idle-offer recovery; abandoned transfers can be replaced against the older acknowledged baseline. Full snapshots are chosen whenever smaller or a patch exceeds nesting bounds.

Both encoded bodies and reconstructed canonical CBOR are bounded to 4 MiB. The decoder bounds nesting through the public SAX interface before accepting container declarations, refuses duplicate keys/trailing objects, and validates the entire reconstructed object before publication. Copy/move patch operations are unsupported. Numeric comparisons preserve CBOR distinctions such as integer/float and signed zero. Transfer counters retain the full supported 53-bit range on 32-bit WASM.

This is a live body-encoding revision: hosts and clients need matching updated implementations. Legacy textual projection bodies are explicitly refused. Content/private setup archive transfer, authenticated packet schema 5, 12 KiB chunking, four outstanding requests, retry timing, projection cadence and service limits retain their existing contracts. The implementation uses the existing pinned library's [CBOR support](https://json.nlohmann.me/api/basic_json/from_cbor/) and [patch application](https://json.nlohmann.me/features/json_patch/); the differ also preserves binary numeric representation.

## Measured permitted payloads

The manual `phoenix_projection_sizes` probe restores the preceding pass's Cruiser rescue checkpoint at tick 600, executes an ordinary tick to rebuild transient diagnostics, and claims each Console through normal admission. It then measures ten updates per Console, two ordinary fixed ticks apart, through the canonical permitted serializer and production binary transfer. Initial observations include each claim as it is admitted; all six claims are present during the update sample. No timing or tuning is frozen in a CI assertion.

Frozen base pack SHA-256: `c86d575ae27be2265699fb0b7ff0052d849b447aea4464380dce2e6c2e300e62`. Update samples cover ticks 603–621. Every one of the 60 reconstructed updates exactly matches its complete permitted observation, without filtering fields, including additional Console models where permitted.

| Console     | Mean full JSON bytes | Initial full CBOR bytes | Mean binary update bytes | Smaller updates |
| ----------- | -------------------: | ----------------------: | -----------------------: | --------------: |
| Captain     |               73,657 |                  51,244 |                    1,127 |          98.47% |
| Comms       |              138,622 |                 106,049 |                   73,327 |          47.10% |
| Engineering |              843,347 |                 643,879 |                   85,262 |          89.89% |
| Helm        |              331,262 |                 371,288 |                   75,870 |          77.10% |
| Science     |            1,205,510 |                 900,373 |                  150,154 |          87.54% |
| Tactical    |              571,199 |                 446,053 |                   75,238 |          86.83% |

Initial CBOR is compared with the initial observation, while the two mean columns compare the same subsequent observations. They are different progress points; in particular, Helm's initial state changes substantially after the other claims. Engineering's largest sampled update is 86,532 bytes. Its mean encoded payload would be approximately 9.1 Mbit/s at ten Hz including base64 expansion, versus 90.0 Mbit/s for the same full-JSON sample. This estimate excludes envelopes, authentication, transport framing and retransmissions; it is not a measured steady-state delivery rate. It does not decompose or reproduce the later 2.4 MB packet from the earlier pass.

The probe records encoding/decoding timings as diagnostics. Sampling overlapped other validation/startup work, so these are not controlled CPU speedup or capacity measurements. Projection construction still uses internal JSON-shaped read models and complete-state hashing; this change does not establish the simulation or presentation performance targets. Unchanged-branch comparison avoids allocating paths or serialising each numeric leaf, and a repeated run produces identical update sizes.

Reproduce with the native-only manual target:

```text
phoenix_projection_sizes base.zip checkpoint.state.json courier breakdown.json
```

## Verification and evidence

The five-client preferred-direct, staggered-startup diagnostic now completes, where the preceding JSON attempts failed admission. Over 30.06 seconds, all five Consoles publish fresh projections, with zero missing-observation polls, receive overflow polls or rejected sends. Captain/Comms/Engineering/Helm/Science publish 103/63/54/57/39 times; maximum publication gaps are 0.50/0.62/0.79/0.66/0.91 seconds. The host accepts 31,169,388 application bytes on direct WebRTC and zero on relay during measurement, approximately 8.3 Mbit/s across five clients. This excludes direct transport framing and is not proof that every accepted byte was delivered; the separate recipient/publication counters establish ongoing reception. There is no matching successful five-client before-run from which to calculate a throughput speedup.

Cold startup remains slow: the host reports 66.00 seconds preparation and 216.50 seconds startup as separate diagnostic fields. The capture advances only 126 simulation ticks. This manual probe requests ticks 50 ms after each tick finishes, so its achieved tick rate and observed publication frequencies do **not** establish production 20 Hz simulation/10 Hz projection capacity. The five-client run has no GM; approved-GM behavior is checked by the synthetic service journey.

A separate one-client forced-relay run completes 30.03 seconds with 32 deliberately dropped state-packet hops (every seventeenth observed hop), 48 fresh Captain publications, no missing-observation polls and a 1.14-second maximum publication gap. Both peers report zero measured receive overflow polls/rejected sends; the host records one startup overflow poll. This is loss/recovery evidence, not a universal loss percentage or steady-state bandwidth improvement against the previous differently paced/progressed run.

- Native, ordinary WASM and threaded WASM wire integration tests pass on the final implementation. Both browser host/client adapters compile independently. These are separate platform checks, not a headed-browser transport receipt.
- Synthetic fixtures cover exact reconstruction, stable metadata omission, metadata edits, authority generations, evidence removal, escaped keys, array changes, nulls, numeric representation, missing/wrong baselines, result-digest refusal, full recovery, malformed/deep/duplicate-key/trailing CBOR, legacy-format refusal, retries and reordered/duplicate chunks.
- All 12 real service journeys pass with none skipped, including live crew/GM admission, replacement, reconnect/fresh-host recovery, fleet checkpoints, verified/refused WSS and lobby setup archives. The subsequent unchanged-branch optimisation preserves the format; final native/WASM tests and the repeated 60-update comparison also pass.
- Module ownership check: 750 working-tree C++ sources, 237 module units, zero exceptions/violations. Owned formatting is checked separately from pre-existing repository drift.

Ignored receipts are under `build/bandwidth-2026-11-02/`: `cruiser-projection-delivery.json`, `size-summary-final.json`, `network-five/traffic.json`, `network-relay-loss/traffic.json`, `native-wire-delivery.log`, `ordinary-wire-delivery.log`, `threaded-wire-delivery.log`, build logs and `network-service.log`. `delivery-receipt.json` records normalized source and executable/JS/WASM/data hashes. Earlier build failures and wrong-working-directory WASM invocations remain separately recorded; the final WASM runs execute beside their packaged fixture data. The existing isolated build directories from the earlier pass are reused for incremental compilation; these new receipts identify the follow-up evidence separately. As in that pass, the captured source includes pre-existing integrated feature work; the measurements describe that working snapshot, not a pristine checkout. Unrelated feature changes remain outside the owned commit, and generated inventories distinguish the committed source from the complete working tree.
