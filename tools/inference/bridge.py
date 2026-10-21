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
    args = parser.parse_args()
    providers = load_providers(json.loads(args.config.read_text(encoding="utf-8")))
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
        reply({"type": "capabilities", "providers": list(providers)})
        for line in child.stdout:
            if len(line) > 65536:
                raise ValueError("Transport request exceeds limit")
            request = json.loads(line)
            started = time.monotonic()
            response = {"type": "result", "id": request["id"]}
            try:
                candidate = providers[request["provider"]].choose(request["snapshot"])
                if candidate not in {item["id"] for item in request["snapshot"]["candidates"]}:
                    raise ValueError("Unoffered candidate")
                response["candidate"] = candidate
            except Exception as error:
                print("Inference failed: " + type(error).__name__, file=sys.stderr)
                response["error"] = "provider_failed"
            response["latency_ms"] = round((time.monotonic() - started) * 1000)
            if args.metrics:
                with args.metrics.open("a", encoding="utf-8") as metrics:
                    metrics.write(json.dumps({"provider": request["provider"], "latency_ms": response["latency_ms"],
                                              "failed": "error" in response}) + "\n")
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
