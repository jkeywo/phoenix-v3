# Simulation and network optimisation, 2 November 2026

This records the authorised eight-hour pass beginning at 00:50:29 UTC. Simulation cost takes priority; network measurements use ordinary admission, permitted projections and real transports. Historical frame-rate claims in [the earlier follow-up](performance-followup.md) are separate measurements.

## Scope and reproducibility

Ignored evidence lives in `build/optimisation-2026-11-02/`. `snapshot.json` and `source-original.zip` preserve the initial working tree at `6a496b36`, including concurrent uncommitted work. The initial content correction is recorded separately in `baseline-prerequisites.json`. Later final comparisons use the integrated scenario revision and matching before/after sources. Captured sources include the pre-existing uncommitted feature/demo work; these receipts describe that frozen working tree, not a pristine checkout of a single commit. That work is present on both sides of the comparison.

The two authorised peer tasks, “Compare scenario objectives to design” and “Review module boundaries”, coordinated through `build/coordination/profiling-window.json`. The first active quiet window ran from 02:59:28 to approximately 03:08 UTC, after both tasks reported readiness and live process checks found no competing builds. The lease was released afterwards. Diagnostic timings gathered during builds are labelled separately.

## Implementation

- Cache rule-source eligibility only within explicitly read-only observation batches. Nested scopes share scratch storage, and the outer scope ends before mutable physical execution or command admission. Live rule activity, expiry and output checks still run.
- Prepare objective candidate categories lazily from the same frozen permitted inputs. Move complete candidate frames into evaluation and reuse diagnostic JSON storage while recomputing every value and removing obsolete evidence.
- Compile immutable observation-name indexes and objective-record destinations once per Controller tree template. Instances share schema metadata, with independent continuation, boards, inboxes and evidence.
- Cache the existing Flecs sensor query. Build immutable rule-trigger presence metadata at content compilation, avoiding source traversal when the catalogue contains no listener for an event.
- Add opt-in inclusive work timings, separately from uninstrumented timing captures. Timing data does not enter authoritative state or choose tick order.

The approach follows Flecs guidance on [cached queries](https://www.flecs.dev/flecs/Queries.html) and [designing with Flecs](https://www.flecs.dev/flecs/DesignWithFlecs.html): reuse long-lived queries and typed compiled relationships, preserve explicit processing phases, and optimise repeated data preparation. This does not add a second scheduler or gameplay implementation. Stable domain ordering remains independent of ECS storage order.

No gameplay frequency, information scope, command ownership, physical cost, policy, message route or save/wire schema is changed by these optimisations. The schema-51 test correction follows the separately committed scenario work; it is not a migration introduced by this pass.

## Final matched-source simulation comparison

The final baseline incorporates the same concurrent scenario/module fixes as the optimised build. Both sides use base archive SHA-256 `c86d575ae27be2265699fb0b7ff0052d849b447aea4464380dce2e6c2e300e62`; combat additionally uses `test-missions.zip`, SHA-256 `2f09d52889f5ef834ca2a65dec5b5bffa8729c8d9b4b57c02ce5c79d3bca5db0`. Each workload has one paired fixed-tick sample. Rendering, pacing and detailed timing instrumentation are absent.

| Workload       | Ticks | Before mean | After mean | Lower mean time | Before / after p95 |
| -------------- | ----: | ----------: | ---------: | --------------: | -----------------: |
| Cruiser rescue |   600 |   218.51 ms |  124.89 ms |          42.85% | 242.26 / 137.64 ms |
| Courier rescue |   600 |   140.26 ms |   78.19 ms |          44.26% | 264.91 / 144.43 ms |
| Cruiser combat |   400 |   349.13 ms |  129.19 ms |          63.00% | 674.72 / 187.70 ms |

Every final pair has byte-identical complete saved state, additionally checked with exact file comparison and SHA-256 (`quiet-final/exact-state-byte-comparison.json`). Rescue samples cover 30 seconds of simulated time; combat covers 20 seconds, including the first wave after its ten-second preparation period. These are workload measurements, not universal speedup claims or shipped-content CI thresholds. All three still exceed the 50 ms simulation budget.

The final coordinated lease ran from 06:16:41 to approximately 07:29 UTC. Timing captures began after the last validation process exited. The first runner attempt stopped on a mistaken child-archive filename; its raw capture/error is retained separately and excluded from the final pairs. Corrected runs include input/executable hashes, timestamps and process inventories in `quiet-final/`.

## Native bridge rerun

All four captures used the actual **NVIDIA GeForce RTX 5090 Laptop GPU**, confirmed by each OpenGL renderer log. Each has five active authored Consoles plus Viewscreen, an actual 5760 × 2160 framebuffer, 120 warm-up frames and a two-minute uncapped measurement. Every measured frame was focused.

| Capture          | Average FPS | Frame p95 | Frame p99 | Ticks during capture |
| ---------------- | ----------: | --------: | --------: | -------------------: |
| Matched baseline |       10.70 | 135.69 ms | 177.60 ms |                  309 |
| Optimised 1      |       10.34 | 139.09 ms | 182.95 ms |                  490 |
| Optimised 2      |       11.23 | 129.86 ms | 166.40 ms |                  512 |
| Optimised 3      |       11.55 | 127.08 ms | 158.55 ms |                  526 |

The earlier 78–79 FPS quote does **not** reproduce on this current integrated tree. The matched baseline is already 10.70 FPS. Simulation work improved, but this pass did not repair presentation throughput: the last optimised run has 75.74 ms median preparation versus 6.58 ms median Console drawing. Nested per-Console timings overlap and must not be summed.

The optimised captures advance approximately 4.1–4.4 simulation ticks per wall-clock second and finish with roughly 98–101 seconds of clock backlog. No elapsed simulation time was discarded to improve the FPS result. Neither the twenty-Hz simulation target nor the sixty-Hz presentation target is met. Screenshots and the full timing breakdowns are beside each JSON capture; these establish six logical regions, not six physical-monitor or input-device acceptance.

## First quiet simulation comparison

Each run executes 600 fixed ticks headlessly, without rendering, pacing or detailed profiling. Both executables use the same frozen base pack, SHA-256 `32776b407f64cbea9c48b49a3f902f48a33cd59a58c85f2ca02b459fcf618794`. The optimised executable at this checkpoint has SHA-256 `a2d97a3863f92b27e15e08137c7255d6636ba4d10cc492ac6ebd89f024de3fe9`. These are intermediate results, before the final allocation-reuse refinement and integrated scenario refresh.

| Rescue workload | Before mean | After mean | Lower mean time | Before / after p95 |
| --------------- | ----------: | ---------: | --------------: | -----------------: |
| Cruiser         |   218.33 ms |  136.59 ms |          37.44% | 239.75 / 155.26 ms |
| Courier         |   140.08 ms |   86.06 ms |          38.56% | 268.02 / 162.69 ms |

For each pair, the **entire parsed saved World after 600 ticks is identical**, without filtering fields. Earlier 200-tick comparisons also matched for each incremental variant. Raw reports, executable receipts and process inventories are in `quiet-1/`. These results do not meet the fixed 50 ms simulation budget.

## Network investigation

The manually invoked `phoenix_network_peer_profile` uses separate native host and client processes, ordinary approval and Console claims, immutable permitted projections and production transports. `network_profile.mjs --multiprocess` starts a real local signalling service behind a size/type observer. Optional `--direct` selects preferred WebRTC; otherwise relay is forced. `PHOENIX_TRANSPORT_PROFILE_DIRECTORY` enables native application byte/route counters. No packet body becomes gameplay knowledge or is written to the traffic report.

The manual traffic probe spaces tick requests 50 ms after the preceding tick returns, keeping its packet loop responsive. Its achieved tick rate is diagnostic pacing, not a production twenty-Hz capacity result. Raw snapshot-size estimates and actual update gaps are therefore reported alongside throughput.

Reports distinguish startup, measured operation and teardown; accepted sends are not proof of delivery. Relay measurements include authenticated application envelopes, base64, relay JSON and estimated WebSocket headers. Native counters exclude transport framing. Neither measurement includes a complete TLS/TCP/IP or ICE/DTLS/SCTP wire capture.

An early separate-process one-client relay capture completed 129 simulation ticks in 30.08 seconds, with 34 Captain publications and no missing-observation polls. Host relay egress averaged approximately 1.43 Mbit/s; the maximum publication gap was 1.20 seconds. Offered snapshots averaged approximately 114 KB of raw JSON. The measured egress reflects the achieved simulation/publication rate, **not** a ten-Hz capacity result.

Five-client and five-client-plus-GM forced-relay startup attempts reached the ordinary admission deadline while builds were running. The logs record signalling rate errors and receive backpressure; they are failed startup evidence, not steady-state bandwidth results. The service bounds remain unchanged. Ship presentation-pack preparation and cold GM content transfer are separate startup costs and remain visible in these captures.

The host suppresses a duplicate offer only when a valid request for that same in-flight immutable projection was served in the current poll. Ordinary idle offers, lost-final-ACK recovery, the 100 ms projection schedule, chunk sizes, retry timing, retention and service limits remain intact. This is a narrow reduction of redundant protocol work, not snapshot compression.

## Final network findings

Both quiet preferred-direct attempts failed the existing diagnostic startup deadline, even with staggered admission. They reached three crew processes; Engineering did not remain stable long enough for the remaining two clients to launch. The intended GM case therefore **never launched its GM**. It establishes no GM capacity result. All launched processes produced counters through graceful diagnostic shutdown; the larger desired-report count describes the clients that were requested, including those never launched.

The first attempt's host accepted about 603 MB of application data on direct WebRTC versus about 4 KB through relay during startup. This proves the selected data route, not successful delivery or steady-state capacity. Receive backpressure occurred. No five-client steady-state throughput number is reported.

Raw offered snapshot sizes during that failed startup were:

| Console address |   Mean raw JSON | Maximum raw JSON | Encoded payload at 10 Hz, mean / maximum |
| --------------- | --------------: | ---------------: | ---------------------------------------: |
| `captain`       |   240,018 bytes |    421,326 bytes |                       25.6 / 44.9 Mbit/s |
| `comms`         |   361,041 bytes |    621,392 bytes |                       38.5 / 66.3 Mbit/s |
| `engineering`   | 1,950,803 bytes |  2,416,465 bytes |                     208.1 / 257.8 Mbit/s |

The last column is an **estimate**: JSON bytes × base64's 4/3 expansion × ten updates × eight bits. It excludes authentication/envelope and transport overhead, retransmissions and static asset transfer. Samples are from different progress points, not a simultaneous fleet-wide maximum. Engineering alone implies approximately 208–258 Mbit/s of encoded payload at ten Hz for these sampled sizes. Snapshot size varies with Console and game progress; the early single-Captain sample was much smaller.

A separate thirty-second relay-loss run completed with 53 deliberately dropped state-packet hops, 16 Captain publications, no missing-observation polls and a 2.53-second maximum publication gap. Drops apply to every seventeenth observed state hop, not a claimed end-to-end loss percentage. A retained old snapshot is not proof of fresh updates. Its lower traffic rate must not be credited as bandwidth efficiency.

### Why the projections are large

The normal wire projection is a full JSON inspection/read model, not an incremental state stream. It repeats action/input descriptions, command bounds, placement metadata, current values, diagnostic candidates and retained knowledge/history. Optional additional Consoles contain further complete scoped observations. `StateSource` serialises the full object as JSON and transfers base64 chunks; it does not compress or delta-encode it.

A new manual diagnostic restores the 600-tick checkpoint, runs one ordinary tick to reconstruct transient diagnostics, claims Consoles through normal admission and measures the canonical permitted serializer. This is a representative **tick-601** breakdown, not a decomposition of the later 2.4 MB packet whose body was intentionally not logged:

| Engineering section                                       |  JSON bytes |
| --------------------------------------------------------- | ----------: |
| Objective-candidate diagnostics: 1,296 rows               |     379,383 |
| Other Controller state/metadata: 95 Controllers           |     173,987 |
| Knowledge records: 264 rows                               |      99,232 |
| Personnel, including assignment routes and 98 duties      |      48,188 |
| Remaining systems, controls, logistics and other sections |     142,592 |
| **Total permitted observation**                           | **843,382** |

Controller rows account for about 66% of that sample; candidate diagnostics alone account for 45%. It also carries 115 control rows and 272 action descriptions. For example, action schemas/descriptions occupy about 53 KB and data-placement maps about 22 KB. These are repeated in ordinary updates despite being largely stable. Other sampled Console observations range from approximately 65 KB to 1.2 MB, including their permitted additional Consoles.

The size is therefore not an irreducible requirement of the physical simulation. It reflects broad read models, verbose/repeated metadata, diagnostics and full-history retransmission, multiplied by the authored Controller/widget count. The remedy is to separate stable scoped metadata from changing values, deliver inspection detail through a supported demand path, and add lossless compression/incremental updates with full resynchronisation. Merging Console knowledge or removing provenance is unnecessary and would violate the intended model. Exact growth of the later 2.4 MB packet still needs a live section-by-section capture; the earlier breakdown does not prove that diagnostics explain all of that growth.

Reproduce with the native-only `phoenix_projection_sizes` target: `phoenix_projection_sizes base.zip checkpoint.state.json courier breakdown.json`. Its output contains sizes, counts and structural paths, not observation values. See `quiet-final/projection-breakdown-live.json`.

Remaining work is substantial: reduce the size and preparation cost of permitted projections, investigate the admission/backpressure failure, and move expensive presentation-pack preparation out of live packet handling through a supported loading boundary. Any compression or incremental protocol must preserve scope, provenance, revisions, withdrawals, generations and full recovery; this pass does not introduce that protocol or claim network capacity is solved.

## Verification and delivery

The full native suite initially passed 52 of 53 cases; its sole failure was a stale schema-50 assertion after the scenario update. Both stale schema tests now require schema 51, with schema 50 explicitly refused. The corrected motion/radar/trees case passes, completing all 53 native cases across the full run and focused rerun.

The existing synthetic Knowledge integration now also compares permitted candidate diagnostics on both Consoles through restore and evidence expiry. It checks regenerated evidence, refusal reasons and scores rather than a production-content golden.

Both complete ordinary and threaded Release WASM builds pass. All 44 final checks in each mode pass, including a separate normal combat run alongside its trace run. All 23 native continuations match both WASM modes: 46 exact continuation comparisons. The two targeted reruns corrected a Windows import URL and a test executable linked just before the schema assertion fix; the ordinary interdiction case also required a longer diagnostic runner deadline after a loaded timeout. Original failure logs remain alongside the final receipts.

All 12 real network service cases pass with none skipped, including majority/minority recovery, portable fleet checkpoints, Console/GM admission, verified/refused WSS and all three lobby sidecar variants. The stale manual-probe schema assertion and a later loaded deadline run are retained separately. The final timing and traffic evidence is recorded above. The repository-wide formatting check reports drift outside this pass in `tests/system/sensors.cpp`, `CMakeLists.txt`, `scripts/prepare-officer-avatars.ps1` and `scripts/prepare-rescue-portraits.ps1`; it is not a passing global formatting gate. Owned changes are checked separately. Physical six-display, screen-reader and touch acceptance remain separate from these checks.

## Delivery and evidence map

- `4a749fb8`: align stale integration assertions with scenario save schema 51.
- `b017c687`: Controller/rule observation preparation and opt-in work timings.
- `f9afb7d0`: correct the portable fleet probe schema assertion.
- `f7b230a4`: redundant-offer elision and reusable native traffic diagnostics.
- `513efa1d`: permitted projection size breakdown diagnostic.
- `quiet-final/*-comparison.json`: final matched simulation results and complete state equality.
- `quiet-final/bridge-*.json`, matching logs/PNGs and `.run.json`: native frame rates, actual adapter, workload and latency evidence.
- `quiet-final/network-five*/traffic.json`: failed admission and direct-route size counters; no steady-state or GM acceptance.
- `validation/*-final-checks.json`, `validation/cross-platform-final.log`: 44 final checks per WASM mode and 46 continuation comparisons.
- `native-full-tests.log`, `native-retest.log`, `network-service-final.log`: native/system and twelve live-service results, with earlier failure logs kept separately.

The source snapshot, matching baseline construction receipt and raw intermediates remain under the ignored experiment directory. No unrelated demo/feature work is included in these optimisation commits. Full source/module inventories are regenerated separately for the committed source and the complete working tree so existing uncommitted feature work remains distinguishable.

The complete working tree inventory is 170,464 physical / 167,178 nonblank maintained C++ lines under `src/`, excluding tests. The committed source view is 170,282 / 166,998 lines; the difference belongs to the existing uncommitted feature work. Physical counts include comments, blank lines, directives and embedded text.
