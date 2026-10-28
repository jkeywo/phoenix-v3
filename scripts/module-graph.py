#!/usr/bin/env python3
"""Generate the offline family/module import graph from current authored C++."""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def graph(root: Path) -> dict:
    inventory = load('graph_inventory', root / 'scripts/module-inventory.py')
    cpp = load('graph_cpp', root / 'scripts/check-module-boundaries.py')
    data, files = inventory.inventory(root)
    owners = {}
    units = []
    for row in files:
        folder = '/'.join(row['source'].split('/')[:3])
        masked = cpp.mask_cpp((root / row['source']).read_text(encoding='utf-8-sig'))
        declaration = next(cpp.DECLARATION.finditer(masked), None)
        declared = declaration.group(2) if declaration else ''
        interface = bool(declaration and (declaration.group(1) or ':' in declared))
        if interface:
            assert declared not in owners, f'Duplicate interface: {declared}'
            owners[declared] = folder
        units.append((row['source'], folder, declared, masked))
    links = {}
    unresolved = []
    for source, folder, declared, masked in units:
        for match in cpp.IMPORT.finditer(masked):
            imported = match.group(1)
            if imported.startswith(':'):
                assert declared, f'Partition import without module: {source}'
                imported = declared.split(':')[0] + imported
            target = owners.get(imported)
            if not target:
                if imported.startswith('phoenix.'):
                    unresolved.append((source, imported))
                continue
            if folder == target:
                continue
            link = links.setdefault((folder, target), {'source': folder, 'target': target, 'imports': set(), 'files': set()})
            link['imports'].add(imported)
            link['files'].add(source)
    assert not unresolved, f'Unresolved imports: {unresolved}'
    nodes = [dict(id=m['folder'], family=m['family'], name=m['module'], lines=m['nonblank_lines'],
                  physical=m['physical_lines'], files=m['source_files'], modules=m['primary_modules'],
                  partitions=m['partitions']) for m in data['modules']]
    edges = [dict(source=edge['source'], target=edge['target'], imports=sorted(edge['imports']),
                  files=sorted(edge['files'])) for _, edge in sorted(links.items())]
    return dict(nodes=nodes, edges=edges, totals=data['totals'],
                fingerprint=data['normalized_source_tree_sha256'],
                revision=subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=root, text=True).strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('docs/module-graph.html'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    data = graph(root)
    template = (root / 'tools/module-graph/template.html').read_text(encoding='utf-8')
    assert template.count('__MODULE_GRAPH_DATA__') == 1
    payload = json.dumps(data, separators=(',', ':'), ensure_ascii=False).replace('<', '\\u003c')
    output = args.output if args.output.is_absolute() else root / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(template.replace('__MODULE_GRAPH_DATA__', payload), encoding='utf-8', newline='\n')
    print(f'{output}: {len(data["nodes"])} module folders, {len(data["edges"])} directed import links, '
          f'{data["totals"]["nonblank_lines"]:,} nonblank C++ lines')


if __name__ == '__main__':
    main()
