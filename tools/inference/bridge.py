"""Own model configuration locally; native helper owns authenticated game transport."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from providers import load_providers


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--pair", required=True)
    parser.add_argument("--service")
    parser.add_argument("--metrics", type=Path)
    parser.add_argument("--max-batch", type=int, choices=range(1, 7))
    parser.add_argument("--decision-interval-ms", type=int, choices=range(500, 2001, 50), default=2000)
    args = parser.parse_args()
    providers = load_providers(json.loads(args.config.read_text(encoding="utf-8")))
    args.max_batch = args.max_batch or min((getattr(p,"max_batch",1) for p in providers.values()),default=1)
    gpu = None
    if "anyjev" in providers and args.metrics:
        from torch import cuda
        gpu = cuda
    command = [str(args.native), "--pair", args.pair]
    if args.service:
        command.extend(["--service", args.service])
    print("Inference providers ready", file=sys.stderr, flush=True)
    child = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             text=True, encoding="utf-8", bufsize=1)
    def reply(value):
        child.stdin.write(json.dumps(value) + "\n")
        child.stdin.flush()
    try:
        reply({"type": "capabilities", "providers": list(providers), "max_batch": args.max_batch, "decision_interval_ms": args.decision_interval_ms})
        for line in child.stdout:
            if len(line.encode("utf-8")) > 320000:
                raise ValueError("Transport request exceeds limit")
            request = json.loads(line)
            if gpu is not None:
                gpu.reset_peak_memory_stats()
            started = time.monotonic()
            response = {"type": "result", "id": request["id"]}
            try:
                snapshots = request.get("snapshots", [request.get("snapshot")])
                if not 1 <= len(snapshots) <= args.max_batch:
                    raise ValueError("Invalid inference batch")
                choices = [None] * len(snapshots)
                for name, provider in providers.items():
                    indices = [i for i, s in enumerate(snapshots) if s["provider"] == name]
                    if not indices:
                        continue
                    states = [snapshots[i] for i in indices]
                    selected = provider.choose_many(states) if hasattr(provider, "choose_many") else [provider.choose(s) for s in states]
                    if len(selected) != len(indices):
                        raise ValueError("Incomplete provider batch")
                    for i, candidate in zip(indices, selected):
                        if candidate not in {item["id"] for item in snapshots[i]["candidates"]}:
                            raise ValueError("Unoffered candidate")
                        choices[i] = candidate
                if any(choice is None for choice in choices):
                    raise ValueError("Unavailable provider")
                if "snapshots" in request:
                    response["candidates"] = choices
                else:
                    response["candidate"] = choices[0]
            except Exception as error:
                print("Inference failed: " + type(error).__name__, file=sys.stderr)
                response["error"] = "provider_failed"
            response["latency_ms"] = round((time.monotonic() - started) * 1000)
            if args.metrics:
                with args.metrics.open("a", encoding="utf-8") as metrics:
                    metrics.write(json.dumps({"provider": request.get("provider", "batch"), "count": len(request.get("snapshots", [request.get("snapshot")])), "latency_ms": response["latency_ms"],
                                              "failed": "error" in response,
                                              "gpu_peak_bytes": gpu.max_memory_allocated() if gpu is not None else None,
                                              "gpu_allocated_bytes": gpu.memory_allocated() if gpu is not None else None}) + "\n")
            reply(response)
    finally:
        child.stdin.close()
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()


if __name__ == "__main__":
    main()
