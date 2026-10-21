# Live AnyJev officers and batching — 21 October 2026

These are explicitly triggered hardware and gameplay observations in the managed
`controller-models` worktree. They do not add maintained-ship timing assertions to
CI. Default Controllers remain on state trees. The fast profile is experimental.

## Setup and scope

RTX 5090 Laptop, 24,463 MiB VRAM, driver 617.14, Windows, Python 3.13,
PyTorch 2.11.0+cu130, Transformers 5.6.0 and AnyJev 0.3.0. Models run in CUDA
BF16 with training-free L0/content-free prior correction and two rotations.

- Qwen3-4B revision `1cfa9a7208912126459214e8b04321603b3df60c`.
- Qwen3-0.6B revision `c1899de289a04d12100db370d81485cdf75e47ca`.

The latter is the publisher's [0.6B model](https://huggingface.co/Qwen/Qwen3-0.6B),
not a quantised 4B. Model weights and measurements remain in ignored `build/`
directories. The GPU initially reported roughly 46–55 MiB occupied and no other
Python compute process was present. Desktop applications and ordinary OS work
remained running; these are laptop observations, not a laboratory guarantee.

Journeys use maintained `base.courier-race` and `base.race-courier` through real
content loading, the host scheduler, authenticated native bridge, AnyJev, shared
admission, continuous action execution and physical resource/motion simulation.
Helm selects engine and steering. Engineering selects core generation,
distribution, reactor cooling and engine power supply. Combined demand selects
all six; other Controllers retain their authored trees.

There were separate 40- and 60-second Helm/Engineering runs, followed by
75-second combined runs. Solo comparisons hold the other Courier's Helm with an
ordinary human claim and Detailed profile. No equipment, ship tuning, map geometry
or physics was changed to make the journey easier. These are headless real-model
journeys, not player-console, browser, screen-reader or six-display acceptance.

## Static six-query comparison

Six independently scoped initial ship snapshots, with the same model and
representation within each comparison:

| Configuration                              |                      Six-query time | Scope                                                                        |
| ------------------------------------------ | ----------------------------------: | ---------------------------------------------------------------------------- |
| 0.6B, serial, prefix reuse                 |                            2,768 ms | One measured pass; includes first question-cache work                        |
| 0.6B, coalesced batch, automatic attention |                      1,258–1,269 ms | Three passes after serial cache preparation                                  |
| 0.6B, explicit cuDNN, compact context      |     927–977 ms warm; 1,837 ms first | Three passes; changed representation, not an isolated kernel-only comparison |
| 4B, prefix reuse, GPU batch 2              | 7,505–7,546 ms warm; 8,894 ms first | Three passes; exceeds the five-second request deadline                       |

The 0.6B cuDNN static pass peaked near 5.52 GB of PyTorch-allocated tensors;
the 4B pass peaked near 11.66 GB. These allocator peaks are not total process or
driver VRAM. Growing live contexts can use more. The pinned Windows wheel reported
`is_flash_attention_available() == False`; an explicit cuDNN SDPA probe and the
model calls succeeded. Automatic dispatch was retained as an option for other
hardware, rather than assuming cuDNN support everywhere.

Raw static evidence: `build/small-coalesced-benchmark.log`,
`build/small-cudnn-benchmark.log`, `build/four-coalesced-benchmark.log` and
`build/flash-check.log`. The committed `tools/inference/benchmark.py` reproduces
serial/batched comparisons on supplied scoped snapshots.

## Findings that changed the implementation

- The initial 4B real officer requests did not produce accepted choices within
  their deadlines. Physical flight in those runs came from tree fallback, and is
  not counted as model competence.
- Larger contexts exposed expensive attention and VRAM growth that the earlier
  tiny synthetic choice benchmark did not reveal. The optional cuDNN profile,
  prefix reuse, independent-query coalescing and compact model representation
  improve throughput. Compacting retains source separation, uncertainty,
  withdrawal and an earlier factual sample, rather than hiding an overload.
- The old timer added inference time to every reconsideration interval. Accepted
  decisions now schedule from snapshot time. A regression checks a reply arriving
  after six simulated ticks against a ten-tick interval.
- Continuously adapting tree actions prevented some concrete model takeovers:
  the returned choice was always stale against a changed tree action serial.
  Takeover now revalidates the prepared action against current availability and
  inputs while keeping authority, orders and coordination checks. `continue` and
  cancellation remain tied to the original activity. This has a synthetic
  production-path regression.
- Race-specific steering candidates needed their own authored role explanation
  and descriptions. The smaller model still needs behaviour assessment; clearer
  wording is not proof of competence. Repeated judgement reports are deduplicated
  across the retained decision window, including intervening continue decisions.
- One bounded GPU job can carry six queries, each with its own scope and decision.
  Authenticated packet parts preserve the existing 64 KiB wire limit. A bounded
  output thread keeps a slow Python worker from blocking bridge heartbeats.

## Final combined confirmation

After the tree-takeover and reconsideration fixes, a 30-second `six-solo` run
completed with all six selected Controllers effective under AnyJev. There were
131 admitted frames: six initial provider-unavailable fallback frames, one
explicit cooling deferral and 124 other choices. None was refused. After the
first ten seconds, per-Controller accepted-choice gaps had medians of
1,297–1,313 ms, nearest-rank 95th percentiles and maxima of 1,649–1,650 ms
(13–14 inter-decision gaps per Controller). These gaps include accepted `continue`
choices; they do not imply a new physical action every time.

The worker completed 19 full six-query jobs: median 1,217 ms and maximum
1,592 ms. Its highest recorded PyTorch allocation was 7,492,777,472 bytes
(about 6.98 GiB), excluding driver/process overhead. The configured minimum
interval was 500 ms. This meets the cadence target in this short observation,
not a sustained worst-case guarantee.

Behaviour did not meet the same standard: steering selected the option to end
destination steering and repeatedly continued it. The ship reached x=-130,340,
y=-40,000 at tick 600, with the finish objective still active. An effective
AnyJev provider is therefore not evidence of competent Helm navigation.
Raw report and per-job measurements: `build/officer-confirm/six-solo.json` and
`build/officer-confirm/metrics.jsonl`.

A preceding 75-second attempt (`build/officer-handoff`) exceeded the harness's
110-second subprocess deadline and has no final gameplay report. Its completed
worker jobs include a 2,355 ms six-query job and a 2,494 ms five-query job. It is not counted as a pass or
silently discarded from the sustained-performance assessment.

## Behaviour and acceptance limits

The authored-tree solo comparison completed the visible finish objective at tick
972 (48.6 simulation seconds). Earlier combined model attempts completed it only
with substantial fallback; another attempt was still short at 75 seconds. The
0.6B model repeatedly deferred steering and sometimes chose conservative engine
supply or an inappropriate navigation mode. Reaching a destination through fallback
does not establish six-officer AnyJev competence.

The short 40-second Engineering run, and then its 60-second run, exercised actual
power/cooling continuation. The 60-second run ended with all four selected
Engineering Controllers effective under AnyJev and live resource values, while the
Helm-only run retained authored steering after model deferral. There was no authored
thermal emergency or damaged-ship stress case in this race journey. General officer
judgement is not accepted from these observations.

Browser pairing, hosted Jev, a loaded llama.cpp backend, 9B comparison, physical
accessibility and six-display frame times remain outside this measurement.

## Verification

The runtime/worker changes are committed as `f0089c4`, and the maintained
journeys, authored role context and experimental profile as `d6678a8`.

- Native: all 29 CTest cases passed, including production-path Controller
  admission/recovery and the five Python adapter cases.
- WASM: the complete system suite and all native/WASM continuation comparisons
  passed, including Controller actions and coordination.
- Authored content: `phoenix_verify build/native/base.zip` accepted the updated
  base pack (19 entity definitions, six maps and 13 Consoles).
- The repeated full native gate passed all ten real-transport cases with no skips,
  including legacy single-query and batched inference, fleet recovery, live
  sessions, TLS and lobby admission.
- Formatting and `git diff --check` passed.

The first combined invocation exposed a test teardown race: a buffered bridge
request could be answered after its stdin was closed. The test now stops its
reply listener before closing the bridge. That invocation also recorded live
session and lobby timeouts during concurrent WASM compilation; the initial
failed log remains in `build/officer-native-check.log`. Final gate logs are
`build/officer-native-recheck.log` and `build/officer-web-check.log`.
