# Controller model providers

State trees remain the default decision provider. A ship host can assign AnyJev,
llama.cpp or Jev to individual Controllers which have authored decision candidates.
Human/AI allocations still come from the Console complexity profile. An assignment
applies only while its Controller is AI-operated. NPCs retain their state trees.

## Start local inference on Windows

Run `./scripts/setup-inference.ps1` explicitly to create an isolated Python
environment under `build/inference-venv`. It installs CUDA PyTorch and pinned
AnyJev dependencies; ordinary builds do not install or start inference.

The initial AnyJev configuration uses Qwen3-4B at revision
`1cfa9a7208912126459214e8b04321603b3df60c`, BF16, CUDA, training-free L0 readout and
content-free prior correction and two option rotations. This bounds inference work; use more rotations when evaluating option-order sensitivity. Model weights download on the first explicit
bridge/smoke invocation. Set `HF_HOME` to a chosen cache directory if needed.
The bridge must finish loading before it can advertise its providers.

1. Build native, including `phoenix_inference_bridge`.
2. Copy `tools/inference/config.example.json` to a private file under `build/`.
3. In the ship host's Settings, open **AI configuration**, then **Pair inference bridge**.
4. Start the bridge with the displayed pairing token:

```powershell
$env:HF_HOME = Join-Path (Get-Location) "build/inference-cache"
$env:HF_HUB_DISABLE_XET = "1"
./build/inference-venv/Scripts/python.exe tools/inference/bridge.py `
  --native build/native/Release/phoenix_inference_bridge.exe `
  --config build/inference.json --pair <token>
```

Use `--service ws://127.0.0.1:9080/phoenix` on the bridge when the game uses that
local signalling service. The default service is the game's normal hosted WSS
service. The authenticated bridge uses the existing direct/relay transport, so a
browser ship host can use the same bridge without connecting to a laptop HTTP
endpoint. Initial pairing expires after two minutes; generate a new token if
loading the model took longer. Configured offline providers remain visible.

Once a provider has advertised, the labelled brain-icon button appears beside
Settings. It is absent from graphics and accessibility navigation when no
external provider is configured. Each Controller offers State tree plus configured
providers; Controllers without candidate metadata explain why models are unavailable.
The panel shows effective provider, fallback reason and last bridge latency.

Tokens, URLs and API keys are host/bridge configuration, never pack content.
Treat the pairing token as a temporary credential. Do not publish it. Provider
keys are read only by the Python process and are never sent to game clients.

## Other providers

A bridge configuration can contain any of these entries:

```json
{
  "anyjev": {
    "model": "Qwen/Qwen3-4B",
    "revision": "1cfa9a7208912126459214e8b04321603b3df60c"
  },
  "llamacpp": {
    "endpoint": "http://127.0.0.1:8080/v1/chat/completions",
    "model": "your-loaded-model"
  },
  "jev": {
    "model": "jev-1.13.0",
    "key_env": "TYPESAFE_API_KEY"
  }
}
```

llama.cpp must support JSON-schema constrained chat completion. The schema allows
only supplied candidate IDs. Use HTTPS for a model endpoint on another machine;
plaintext HTTP is accepted only on loopback. Jev uses its official System One
endpoint and requires a versioned model ID. No provider receives actuator authority.

For a 9B comparison, use a text-serving GGUF conversion of
[Qwen3.5-9B](https://huggingface.co/Qwen/Qwen3.5-9B) with a compatible llama.cpp
build. Record the conversion repository revision, file SHA-256 and llama.cpp
commit; this conversion/runtime combination has not been measured here. The
upstream checkpoint is multimodal, so replacing the AnyJev model name alone is
not a verified configuration.

```powershell
llama-server -m <pinned-9b.gguf> --host 127.0.0.1 --port 8080 `
  --ctx-size 8192 --n-gpu-layers 99 --alias phoenix-9b
```

Set the bridge's `llamacpp.model` to `phoenix-9b`. Export the same synthetic snapshot
and run the explicit smoke and six-Controller probe below for both models,
restarting the bridge between them. Record cold load, warm latency, five-second
expiry rate, full Controller revisit interval, VRAM and rendering frame times.
Use one model at a time first; only then measure both resident together. BF16 9B
weights leave substantially less of the laptop's 24 GB for context and rendering.

## Authoring prepared choices

Controller policies may declare `decision_candidates` and a `decision_context` string of up to 4096 bytes explaining their role and relevant mechanics. Existing `action_bindings`
still identify shared execution trees. A candidate names a bounded description,
optional availability conditions, an optional authored deviation reason, and one
to four typed policy tasks. Supported tasks select/replace/cancel shared actions,
use the officer coordination protocol, or acknowledge an addressed message.
Candidates cannot write commands, call policies, or access private policy memory.

```json
{
  "decision_candidates": [{
    "id": "cruise",
    "description": "Cruise toward the known objective at the prepared speed.",
    "available": [],
    "tasks": [{
      "action": "select.transit",
      "arguments": ["transit", true,
        {"observation": "objective.x"}, {"observation": "objective.y"}, 5000]
    }]
  }]
}
```

The existing typed compiler checks task arguments and observation references.
Availability is reevaluated before admission. Workshop edits these records through
the Controller tree editor. Candidate arguments are immutable prepared values;
models cannot create coordinates, targets or resource amounts.

Place important commitments before optional alternatives. The request carries up
to 24 authored choices plus `continue` and `defer_to_tree`, reporting any omitted
choices. Current work remains available through `continue`. Generated officer
support choices precede routine alternatives. Action labels and named input values
accompany the choice, so models do not have to infer meaning from local IDs.

## Execution, scope and recovery

The shared Controller decision admission handles state-tree and model selections.
One action remains active per Controller. Repeating a selection continues it;
replacement or cancellation runs ordinary cleanup. Machinery and resource
constraints determine actual execution. Completion must be based on observations.

Snapshots contain scoped facts, provenance-bearing evidence, relevant messages,
orders/stances, current action identity and status, coordination obligations and a
bounded decision history. Another Controller's private inbox and policy memory are
excluded. Retained evidence lists are bounded and include omission counts. Context
over the transport limit is refused rather than sent as an unbounded packet.

Models choose the full policy, including coordination. The bridge warms the local model before advertising readiness so lazy GPU initialisation does not consume a game request deadline. Explicit orders remain
context for officer judgement. Authored deviation reasons produce deduplicated
internal reports when standing orders/objectives are active. Internal communication
uses existing localised templates; generated prose and external Comms are deferred.

Hosts reconsider every two simulation seconds, coalescing material changes to at
most twice per wall-clock second. A bridge handles one GPU job at a time, fairly rotating Controllers. A job can contain up to six independent queries. Requests expire after five seconds. Valid current work
continues while waiting. Failure returns policy control to the authored tree;
recovery retries use 2, 4, 8, 16 and then 30 simulation seconds. A fresh validated
decision automatically resumes the selected provider.

Only the owning ship host requests inference. Decisions, assignments and failure
transitions enter the durable input stream. Replicas and journal replay execute
committed choices without contacting models. Operator/provider generations,
request identity, action status, order revisions and coordination state reject
obsolete replies. Replacing a world invalidates pending bridge requests.

Schema 14 stores provider selection, effective state, sequence and bounded history
alongside existing action continuation. Schema 13 migrates to default state-tree
operation, with the single appended officer-report schema checked explicitly.
Credentials and pending inference requests are not saved.

## Validation and explicit smoke

Automatic native/WASM Controller integrations cover prepared choices through real
machinery, duplicate refusal, handover, continuation, migration and fallback.
Presentation checks cover hidden/unavailable controls and host authority. The
network gate exercises a real native inference bridge with a synthetic provider;
Python adapter tests exercise a local constrained-output HTTP endpoint.

For hardware measurements, export the synthetic production-path snapshot and run:

```powershell
./build/native/Release/phoenix_inference_probe.exe --snapshot > build/inference-snapshot.json
./build/inference-venv/Scripts/python.exe tools/inference/smoke.py `
  --config build/inference.json --snapshot build/inference-snapshot.json --count 6
```

For a live bridge journey, run `phoenix_inference_probe <token> <service-url> 30 6`
alongside the real bridge, using the same token and service. Its final report
contains admitted decisions and physical command values. The last argument selects one to six independent synthetic Controllers. Pass `--metrics build/inference-metrics.jsonl` to the Python bridge to record each inference duration without recording its context. Compare rendering frame
captures with and without inference using the native performance capture options.
These are explicitly triggered smoke evidence, not CI timing thresholds. A
successful build does not establish model competence, six-display performance or
screen-reader usability.

## Batched local queries and an experimental fast profile

The host can now gather up to six Controller queries into one bridge job. Each
query keeps its own scoped snapshot, candidate set and admission check; Console
observations are never concatenated into a shared officer prompt. AnyJev prepares
queries concurrently and coalesces their scoring into one GPU call on the worker
thread. Compatible questions share the public `decide_batch` path. Different
candidate counts still use independent Deciders and are batched at the HF scoring
boundary. One model instance remains resident; GPU execution is serialised.

Up to six individually bounded snapshots travel as authenticated 48 KB parts,
within the existing 64 KiB packet limit. The bridge assembles only one bounded job.
Its output queue holds the newest waiting job, so a slow worker cannot block
transport heartbeats. Five-second request expiry, stale-reply rejection and tree
fallback still apply to every Controller. Legacy single-query bridges work too.

The default Qwen3-4B profile retains a small GPU batch and two-second request
interval. `config.fast.example.json` is an **experimental** Qwen3-0.6B profile at
revision `c1899de289a04d12100db370d81485cdf75e47ca`, BF16, batch size 12, two L0
rotations and explicit cuDNN attention. It is a speed/quality experiment, not a
validated replacement for authored officer policies. See the [live measurements
and behaviour limits](smoke/controller-model-live.md) before choosing it.

```powershell
$env:HF_HOME = Join-Path (Get-Location) "build/inference-cache"
$env:HF_HUB_DISABLE_XET = "1"
./build/inference-venv/Scripts/python.exe tools/inference/bridge.py `
  --native build/native/Release/phoenix_inference_bridge.exe `
  --config tools/inference/config.fast.example.json --pair <token> `
  --decision-interval-ms 500
```

The optional interval is bounded to 500–2,000 ms and defaults to 2,000 ms. Its
accepted simulation interval is committed with the decision. Reconsideration is
measured from the snapshot time, so inference latency is not added again. No
Controller may have more than one outstanding query, even with the shorter
interval. `--max-batch 1` selects serial requests for comparison; the normal maximum
is derived from the configured GPU batch and is capped at six.

The pinned Windows PyTorch wheel on the measured laptop reported no Flash
Attention kernel. The fast profile explicitly requests its working cuDNN path;
unsupported hardware fails rather than silently choosing a different kernel.
`attention: "auto"` retains framework dispatch. These are backend settings, not
simulation permissions.

Prompts retain standing orders, commitments, scoped values and uncertainty while
factoring repeated provenance. Repeated factual revisions retain the newest value
and its provenance plus an earlier retained sample; conflicting claims and other
sources remain separate. Omission counts are explicit. The authoritative snapshot
and prepared action bundles are unchanged. Only the model-facing representation
is compacted.

An adapting fallback tree may change its current activity while a query runs. A
concrete takeover is accepted only after its prepared action is revalidated, with
operator/provider authority, orders and commitments unchanged. `continue` and
cancellation still require the original activity. Explicit model deferral displays
`provider_deferred`, rather than looking like a transport or model failure.

## Reproduce the officer journeys

These are explicit manual smoke commands through maintained content, the real
host scheduler, authenticated bridge, admission and physical simulation:

```powershell
./build/native/Release/phoenix_inference_probe.exe --officer-snapshots `
  build/native/base.zip > build/officer-snapshots.json
./build/inference-venv/Scripts/python.exe tools/inference/benchmark.py `
  --config tools/inference/config.fast.example.json `
  --snapshots build/officer-snapshots.json --serial
./build/native/Release/phoenix_inference_probe.exe --officers `
  build/native/base.zip <token> <service-url> 75 six-solo
```

Use `helm`, `engineering` or `six` for the competitive race. Add `-solo` to hold the
other ship's Helm through an ordinary human claim, removing race-result ambiguity.
`tree-solo` is the authored-policy comparison. The six selected Controllers are
engine, steering, core generation, core distribution, reactor cooling and engine
power supply. Reports include all admission/refusal events, physical position
samples, resource continuation, scoped final snapshots and the visible objective
outcome. A journey process exiting successfully does not certify officer competence.
