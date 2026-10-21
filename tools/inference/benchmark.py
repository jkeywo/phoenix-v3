"""Explicit AnyJev hardware benchmark; never run by the automatic gate."""
import argparse
import json
from pathlib import Path
import platform
import statistics
import time
from providers import load_providers, context


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--snapshots', type=Path, required=True)
    parser.add_argument('--count', type=int, default=6)
    parser.add_argument('--serial', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.count <= 100:
        parser.error('count must be 1..100')
    snapshots = json.loads(args.snapshots.read_text(encoding='utf-8-sig'))
    if not isinstance(snapshots, list) or not 1 <= len(snapshots) <= 6:
        parser.error('provide one to six independent scoped snapshots')
    config = json.loads(args.config.read_text(encoding='utf-8-sig'))
    if set(config) != {'anyjev'}:
        parser.error('benchmark one AnyJev model at a time')
    import torch
    started = time.monotonic()
    provider = load_providers(config)['anyjev']
    print(json.dumps({'type': 'loaded', 'seconds': time.monotonic() - started, 'model': config['anyjev'],
                      'gpu': torch.cuda.get_device_name(), 'torch': torch.__version__, 'python': platform.python_version(),
                      'snapshots': len(snapshots), 'prompt_characters': [len(json.dumps(context(s))) for s in snapshots]}), flush=True)
    for mode in (['serial', 'batch'] if args.serial else ['batch']):
        values = []
        for trial in range(args.count + 1):
            torch.cuda.reset_peak_memory_stats()
            torch.cuda.synchronize()
            started = time.monotonic()
            choices = ([provider.choose(s) for s in snapshots] if mode == 'serial' else provider.choose_many(snapshots))
            torch.cuda.synchronize()
            elapsed = (time.monotonic() - started) * 1000
            if trial:
                values.append(elapsed)
            print(json.dumps({'mode': mode, 'warmup': trial == 0, 'latency_ms': elapsed, 'choices': choices,
                              'allocated_bytes': torch.cuda.memory_allocated(), 'peak_bytes': torch.cuda.max_memory_allocated()}), flush=True)
        print(json.dumps({'mode': mode, 'median_ms': statistics.median(values), 'max_ms': max(values)}), flush=True)


if __name__ == '__main__':
    main()
