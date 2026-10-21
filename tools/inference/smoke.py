"""Explicit, hardware-dependent smoke. Never part of the automatic test gate."""
import argparse
import json
from pathlib import Path
import statistics
import time
from providers import load_providers


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--snapshot", type=Path, required=True)
    parser.add_argument("--count", type=int, default=6)
    args = parser.parse_args()
    started = time.monotonic()
    providers = load_providers(json.loads(args.config.read_text(encoding="utf-8")))
    loaded = time.monotonic() - started
    snapshot = json.loads(args.snapshot.read_text(encoding="utf-8-sig"))
    import torch
    results = []
    for name, provider in providers.items():
        for i in range(args.count):
            started = time.monotonic()
            choice = provider.choose(snapshot)
            elapsed = (time.monotonic() - started) * 1000
            results.append({"provider": name, "candidate": choice, "latency_ms": elapsed})
    values = sorted(item["latency_ms"] for item in results)
    print(json.dumps({"load_seconds": loaded, "gpu": torch.cuda.get_device_name(),
                      "torch": torch.__version__, "allocated_bytes": torch.cuda.memory_allocated(),
                      "peak_bytes": torch.cuda.max_memory_allocated(), "median_ms": statistics.median(values),
                      "max_ms": max(values), "decisions": results}))


if __name__ == "__main__":
    main()
