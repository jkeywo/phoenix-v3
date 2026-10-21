# Controller inference smoke — 21 October 2026

Implementation branch: `controller-models`, based on `6a820b6`. Implementation edits and downloaded model artifacts are kept in the separate managed worktree; the bundled build tools are reused through a junction.

## Hardware and model

Windows laptop, NVIDIA GeForce RTX 5090 Laptop GPU, 24,463 MiB reported memory,
driver 617.14. Python 3.13, PyTorch 2.11.0+cu130, Transformers 5.6.0 and AnyJev
0.3.0. Qwen/Qwen3-4B revision `1cfa9a7208912126459214e8b04321603b3df60c`,
CUDA BF16, batch size 2, L0/content-free prior, two rotations in the final run.
An unrelated Python GPU workload was running and was left alone. These are shared
machine observations, not isolated capacity measurements.

The explicit standalone initial run loaded in 22.62 seconds. PyTorch reported
8,110,229,504 allocated bytes and 8,882,302,464 peak allocated bytes. Its first
real decision took 18.95 seconds; subsequent decisions took 421–959 ms. The
bridge now warms the model before advertising readiness. Startup warm-up is
not a guarantee that every subsequent context will fit a five-second deadline.

## Real transport and simultaneous demand

A separately launched Python/AnyJev worker and native authenticated bridge served
six synthetic Controllers through the production host scheduler, snapshot,
admission and engine command paths for 24 seconds. A separate playable native
renderer ran concurrently. The six Controllers ended with effective AnyJev and
accepted `faster` decisions, producing the prepared 5,500 mm/s request. Each had
four accepted decisions. This establishes this simple choice journey, not Helm
navigation competence or Engineering prioritisation.

The bridge recorded 25 requests: median 868 ms, minimum 792 ms, maximum 9,883 ms.
The 24 completed gameplay requests took 792–1,127 ms. The last request finished after the probe host had exited and took 9,883 ms; no host admission or timeout result was observed for that final request. Controller 0's accepted ticks were 59, 159, 263 and 371: a
roughly five-second revisit cadence under six-way demand. The requested two-second
cadence is **not achieved for simultaneous demand**. Increasing model throughput
or reducing the active Controller set needs further measurement.

This run exposed and fixed a watchdog error: merely waiting behind other
Controllers must not cause fallback. Only actual outstanding requests expire.
A production-path regression now keeps a valid action running for 110 ticks
without a new model reply.

## Rendering

Two 1,200-frame native captures at 1280×720 with a 60 Hz limiter:

| Capture              | Frame p50 | Frame p95 | Frame p99 |   Maximum |
| -------------------- | --------: | --------: | --------: | --------: |
| Baseline             | 16.667 ms | 16.843 ms | 17.114 ms | 17.357 ms |
| Concurrent inference | 16.668 ms | 17.722 ms | 21.154 ms | 29.256 ms |

These are CPU wall/frame times including frame limiting. The capture reports zero
dedicated player Console regions; it uses the shared sidebar. It does not establish
six 1080p displays at 60 Hz, GPU timing, touch routing or screen-reader usability.

Raw local evidence (ignored build outputs): `build/inference-live-final/` contains
`baseline.json`, `with-inference.json`, `metrics.jsonl` and `journey.json`.
`build/anyjev-smoke.json` contains the earlier allocator/load measurement.

## Acceptance limits

Live hosted Jev was not exercised because credentials are unavailable. llama.cpp
has a constrained-HTTP adapter test, but no live loaded llama.cpp model was measured.
The 9B comparison remains a documented recipe, not measured evidence. The real
browser-to-local-model pairing journey, full authored Helm/Engineering officer
behaviour and physical accessibility acceptance remain unverified. Automated
WASM compilation and deterministic continuation are recorded separately from
those live acceptance journeys.

## Verification receipt

The native suite passes all 29 cases, with content/build checks and all nine
real-transport service cases. A separate native configuration-panel capture using
an advertised llama.cpp capability fixture was inspected; it establishes grouping,
provider controls and hidden pairing-token presentation, not live llama.cpp inference.
The native Controller regression also replays committed model assignments,
choices and ticks in a second production GameExecution without an inference
connection and checks exact saved-state equality.

The complete WASM gate passes, including all gameplay cases and matching native/WASM
simulation, Controller (with model replay), combat, Comms, sensors, away and FTL
continuations. The production base pack verifies 19 entity definitions, six maps
and 13 Consoles. Formatting checks pass. Automatic logs are kept locally under
`build/final-native-ctest.log`, `build/final-native-check.log` and
`build/final-web-check.log`.

The final native toolbar capture (`build/ai-button.png`) confirms the labelled AI
control beside Settings; `build/ai-configuration.png` shows the grouped settings
panel. Both use the explicit manual smoke path. Maintained content supplies 149
choices across 51 policies; this is authored coverage, not a competence score.
