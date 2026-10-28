#!/usr/bin/env python3
"""Report authored C++ reading sizes; optionally refresh module inventory documents.

No size limits or gameplay/performance assertions are imposed. A folder is a
reading module; named modules, private partitions and compiled targets differ.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys

SUFFIXES = {'.cpp', '.cppm', '.hpp', '.cc', '.cxx', '.h', '.hxx'}
SKIP = {'vendor', 'third_party', 'generated', 'build', 'node_modules', '_deps', 'references'}


def source_path(name: str) -> bool:
    path = PurePosixPath(name)
    return path.suffix in SUFFIXES and not SKIP.intersection(path.parts)


def baseline(root: Path, revision: str) -> dict:
    names = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', revision, '--', 'src'], cwd=root, text=True).splitlines()
    names = [name for name in names if source_path(name)]
    batch = subprocess.run(['git', 'cat-file', '--batch'], cwd=root,
                           input=''.join(revision + ':' + name + '\n' for name in names).encode(),
                           capture_output=True, check=True).stdout
    offset = physical = nonblank = 0
    for name in names:
        end = batch.index(b'\n', offset)
        size = int(batch[offset:end].split()[-1])
        lines = batch[end + 1:end + size + 1].decode('utf-8-sig').splitlines()
        offset = end + size + 2
        physical += len(lines)
        nonblank += sum(bool(line.strip()) for line in lines)
    return {'revision': revision, 'source_files': len(names), 'physical_lines': physical, 'nonblank_lines': nonblank}


def inventory(root: Path) -> tuple[dict, list[dict]]:
    spec = importlib.util.spec_from_file_location('inventory_cpp', Path(__file__).with_name('check-module-boundaries.py'))
    cpp = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = cpp
    spec.loader.exec_module(cpp)
    rows, groups, families = [], {}, {}
    tree = hashlib.sha256()
    for path in sorted((root / 'src').rglob('*')):
        relative = path.relative_to(root).as_posix()
        if not path.is_file() or not source_path(relative):
            continue
        text = path.read_text(encoding='utf-8-sig')
        parts = PurePosixPath(relative).parts
        if len(parts) < 4:
            raise ValueError(f'Source needs a family/module folder: {relative}')
        digest = hashlib.sha256(text.encode('utf-8')).hexdigest()
        tree.update(relative.encode() + b'\0' + digest.encode() + b'\n')
        declaration = next(cpp.DECLARATION.finditer(cpp.mask_cpp(text)), None)
        name = declaration.group(2) if declaration else ''
        interface = bool(declaration and (declaration.group(1) or ':' in name))
        lines = text.splitlines()
        row = dict(source=relative, family=parts[1], module=parts[2], physical_lines=len(lines),
                   nonblank_lines=sum(bool(line.strip()) for line in lines),
                   kind='interface' if interface else 'header' if path.suffix in {'.hpp', '.h', '.hxx'} else 'implementation',
                   primary_module=name if interface and ':' not in name else '',
                   partition=name if interface and ':' in name else '', normalized_sha256=digest)
        rows.append(row)
        folder = '/'.join(parts[:3])
        group = groups.setdefault(folder, dict(family=parts[1], module=parts[2], folder=folder, source_files=0,
                                              physical_lines=0, nonblank_lines=0, primary_modules=[], partitions=[]))
        group['source_files'] += 1
        for key in ('physical_lines', 'nonblank_lines'):
            group[key] += row[key]
        if row['primary_module']:
            group['primary_modules'].append(row['primary_module'])
        if row['partition']:
            group['partitions'].append(row['partition'])
    for group in groups.values():
        family = families.setdefault(group['family'], dict(family=group['family'], module_folders=0, source_files=0,
                                                         primary_modules=0, partitions=0, physical_lines=0, nonblank_lines=0))
        family['module_folders'] += 1
        family['primary_modules'] += len(group['primary_modules'])
        family['partitions'] += len(group['partitions'])
        for key in ('source_files', 'physical_lines', 'nonblank_lines'):
            family[key] += group[key]
    totals = dict(module_folders=len(groups), source_files=len(rows),
                  primary_modules=sum(len(g['primary_modules']) for g in groups.values()),
                  partitions=sum(len(g['partitions']) for g in groups.values()),
                  physical_lines=sum(r['physical_lines'] for r in rows), nonblank_lines=sum(r['nonblank_lines'] for r in rows))
    return dict(totals=totals, normalized_source_tree_sha256=tree.hexdigest(),
                families=sorted(families.values(), key=lambda f: f['family']),
                modules=sorted(groups.values(), key=lambda g: g['folder'])), rows


def write_inventory(root: Path, data: dict, rows: list[dict]) -> None:
    docs = root / 'docs'
    with (docs / 'module-files.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    fields = ['family', 'module', 'folder', 'source_files', 'primary_module_count', 'private_partition_count',
              'physical_lines', 'nonblank_lines', 'primary_modules', 'private_partitions']
    with (docs / 'module-sizes.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator='\n')
        writer.writeheader()
        for group in data['modules']:
            writer.writerow({key: group[key] for key in fields[:4] + fields[6:8]} |
                            dict(primary_module_count=len(group['primary_modules']), private_partition_count=len(group['partitions']),
                                 primary_modules='; '.join(group['primary_modules']), private_partitions='; '.join(group['partitions'])))
    totals = data['totals']
    text = '''# Source size inventory

This is a source/reading snapshot, **not a runtime or platform acceptance receipt**. See [migration evidence](modularisation-evidence.md) and [the refinement record](understandability-refactor.md) for checks actually run.

- Scope: first-party C++ under `src/`; tests, historical references, dependencies, generated output, CMake, Markdown and external web assets are excluded. All maintained platform variants count.
- Physical lines include blanks. Nonblank lines include comments, directives and embedded JavaScript/JSON/text; these are reading sizes rather than comment-stripped logical statements.
- A module row is a family/module folder, which can contain several named modules/targets. Private partitions are counted separately. Counts do not establish target independence.
- The World `native_headers.hpp` files contain standard/third-party includes only; ordinary CPP units own implementation.
- Reproduce with `python scripts/module-inventory.py --write --baseline 615e2ff`, then format the Markdown. The baseline option reports growth without imposing a quota.

'''
    text += (f"Totals: **{totals['source_files']:,} files**, **{totals['module_folders']} module folders**, "
             f"**{totals['primary_modules']} primary named modules + {totals['partitions']} private partitions**, "
             f"**{totals['physical_lines']:,} physical lines**, **{totals['nonblank_lines']:,} nonblank lines**.\n\n")
    text += 'Normalized source fingerprint (sorted paths and UTF-8 contents, CRLF normalized to LF): `' + data['normalized_source_tree_sha256'] + '`.\n\n'
    text += 'Machine-readable [module sizes](module-sizes.csv) and [individual files/hashes](module-files.csv) contain the same counts.\n\n## Families\n\n| Family | Folders | Files | Primary / private partitions | Physical | Nonblank |\n| --- | ---: | ---: | ---: | ---: | ---: |\n'
    for family in data['families']:
        text += f"| `{family['family']}` | {family['module_folders']} | {family['source_files']} | {family['primary_modules']} / {family['partitions']} | {family['physical_lines']:,} | {family['nonblank_lines']:,} |\n"
    text += '\n## Modules within each family\n\nRead the module README for its interface and dependency direction. Private World integration remains coupled native composition; the folder split does not claim every policy is an independent domain library.\n'
    for family in data['families']:
        text += '\n### ' + family['family'] + '\n\n| Module | Files | Physical | Nonblank | Primary / private partitions |\n| --- | ---: | ---: | ---: | ---: |\n'
        for group in data['modules']:
            if group['family'] == family['family']:
                text += f"| [{group['module']}](../{group['folder']}/README.md) | {group['source_files']} | {group['physical_lines']:,} | {group['nonblank_lines']:,} | {len(group['primary_modules'])} / {len(group['partitions'])} |\n"
    (docs / 'module-sizes.md').write_text(text, encoding='utf-8', newline='\n')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--write', action='store_true', help='refresh the three docs/module-* inventory files')
    parser.add_argument('--baseline', help='compare totals with a Git revision without changing it')
    args = parser.parse_args()
    root = args.root.resolve()
    data, rows = inventory(root)
    if args.write:
        write_inventory(root, data, rows)
    result = dict(data['totals'], normalized_source_tree_sha256=data['normalized_source_tree_sha256'])
    if args.baseline:
        before = baseline(root, args.baseline)
        result['baseline'] = before
        result['growth'] = {key: result[key] - before[key] for key in ('source_files', 'physical_lines', 'nonblank_lines')}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
