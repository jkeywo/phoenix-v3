#!/usr/bin/env python3
"""Check first-party C++ module ownership without compiling or reading build output.

This is an architecture check, not a C++ parser. It recognises module declarations,
imports and local includes after masking comments/literals. The compiler remains
responsible for C++ semantics and CMake for the actual target dependency graph.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


SOURCE_SUFFIXES = {".cpp", ".cppm", ".hpp", ".cc", ".cxx", ".h", ".hxx"}
NAME = r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*"
DECLARATION = re.compile(rf"(?m)^\s*(export\s+)?module\s+({NAME}(?::{NAME})?)\s*;")
IMPORT = re.compile(rf"(?m)^\s*(?:export\s+)?import\s+(:?{NAME}(?::{NAME})?)\s*;")
LOCAL_INCLUDE = re.compile(r'(?m)^[ \t]*#[ \t]*include[ \t]*(?:\\\r?\n[ \t]*)?"([^"\r\n]+)"')
CLASS_OPEN = re.compile(
    r"\b(?:class|struct|union)\s+(?:[A-Za-z_]\w*\s*(?:final\s*)?)?(?::[^;{}]+)?\{"
)
RAW_STRING = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(')


@dataclass(frozen=True, order=True)
class Diagnostic:
    source: str
    line: int
    rule: str
    target: str
    message: str


@dataclass
class Unit:
    path: Path
    source: str
    owner: str
    text: str
    masked: str
    declaration: str = ""
    interface: bool = False


def mask_cpp(text: str) -> str:
    """Preserve positions/newlines while removing comments and literal contents."""
    result = list(text)

    def erase(start: int, end: int) -> None:
        for at in range(start, end):
            if result[at] not in "\r\n":
                result[at] = " "

    at = 0
    while at < len(text):
        if text.startswith("//", at):
            end = text.find("\n", at + 2)
            end = len(text) if end < 0 else end
        elif text.startswith("/*", at):
            end = text.find("*/", at + 2)
            end = len(text) if end < 0 else end + 2
        elif raw := RAW_STRING.match(text, at):
            closing = ")" + raw.group(1) + '"'
            end = text.find(closing, raw.end())
            end = len(text) if end < 0 else end + len(closing)
        elif text[at] in "\"'":
            # Apostrophes in numeric literals (including hex) are not characters.
            if text[at] == "'" and re.search(r"\b[0-9][0-9A-Za-z_]*$", text[:at]):
                at += 1
                continue
            quote = text[at]
            end = at + 1
            while end < len(text):
                if text[end] == "\\":
                    end += 2
                elif text[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
            end = min(end, len(text))
        else:
            at += 1
            continue
        erase(at, end)
        at = end
    return "".join(result)


def source_line(text: str, position: int) -> int:
    return text.count("\n", 0, position) + 1


def module_folder(relative: Path) -> str:
    return "/".join(relative.parts[:3]) if len(relative.parts) >= 4 else ""


def inside_class(masked: str, positions: list[int]) -> set[int]:
    """Find include offsets nested in class bodies, including method bodies."""
    class_braces = {match.end() - 1 for match in CLASS_OPEN.finditer(masked)}
    events = [(match.start(), match.group()) for match in re.finditer(r"[{}]", masked)]
    events.extend((position, "include") for position in positions)
    stack: list[bool] = []
    nested: set[int] = set()
    for position, event in sorted(events):
        if event == "{":
            stack.append(position in class_braces)
        elif event == "}":
            if stack:
                stack.pop()
        elif any(stack):
            nested.add(position)
    return nested


def load_exceptions(path: Path | None) -> list[dict[str, str]]:
    if path is None:
        return []
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if value.get("version") != 1 or not isinstance(value.get("exceptions"), list):
        raise ValueError("exception file must have version 1 and an exceptions array")
    result: list[dict[str, str]] = []
    identities: set[tuple[str, str, str]] = set()
    for exception in value["exceptions"]:
        fields = ("source", "rule", "target", "reason", "chunk")
        if not isinstance(exception, dict) or any(not isinstance(exception.get(key), str) for key in fields):
            raise ValueError("each exception needs string source, rule, target, reason and chunk")
        if not exception["reason"].strip() or not exception["chunk"].strip():
            raise ValueError("each exception needs a nonempty reason and migration chunk")
        if any(char in exception["source"] + exception["target"] for char in "*?[]"):
            raise ValueError("exceptions must name exact sources and targets, not patterns")
        identity = (exception["source"], exception["rule"], exception["target"])
        if identity in identities:
            raise ValueError(f"duplicate exception: {identity}")
        identities.add(identity)
        result.append(exception)
    return result


def inspect(root: Path) -> tuple[list[Diagnostic], int, int]:
    root = root.resolve()
    source_root = root / "src"
    if not source_root.is_dir():
        raise ValueError(f"source directory does not exist: {source_root}")
    units: list[Unit] = []
    diagnostics: list[Diagnostic] = []
    source_count = 0

    def report(unit: Unit, position: int, rule: str, target: str, message: str) -> None:
        diagnostics.append(Diagnostic(unit.source, source_line(unit.text, position), rule, target, message))

    for path in sorted(source_root.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        source_count += 1
        relative = path.relative_to(root)
        try:
            text = path.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError as error:
            diagnostics.append(Diagnostic(relative.as_posix(), 1, "source-encoding", "utf-8", f"source must be UTF-8 ({error.reason} at byte {error.start})"))
            continue
        unit = Unit(path, relative.as_posix(), module_folder(relative), text, mask_cpp(text))
        units.append(unit)
        if not unit.owner:
            report(unit, 0, "folder-layout", "", "C++ source must live under src/<family>/<module>/")
        declarations = list(DECLARATION.finditer(unit.masked))
        if len(declarations) > 1:
            report(unit, declarations[1].start(), "multiple-declarations", "", "one named-module unit per source file")
        if declarations:
            declaration = declarations[0]
            unit.declaration = declaration.group(2)
            unit.interface = bool(declaration.group(1))

    owners: dict[str, Unit] = {}
    for unit in units:
        # An implementation partition is a uniquely owned importable unit too.
        if not unit.declaration or (not unit.interface and ":" not in unit.declaration):
            continue
        if previous := owners.get(unit.declaration):
            report(unit, 0, "duplicate-module", unit.declaration, f"already declared by {previous.source}")
        else:
            owners[unit.declaration] = unit

    graph: dict[str, set[str]] = {name: set() for name in owners}
    for unit in units:
        if unit.declaration:
            primary = unit.declaration.split(":", 1)[0]
            if primary not in owners:
                report(unit, 0, "missing-interface", primary, "named implementation has no primary interface")
            elif unit.owner != owners[primary].owner:
                # A private interface partition owns a composition folder only
                # within its primary module's family. Ordinary units there must
                # directly import that folder's partition; importing an unrelated
                # partition or crossing a family never grants ownership.
                same_family = unit.owner.split("/")[:2] == owners[primary].owner.split("/")[:2]
                owned_partition = ":" in unit.declaration and unit.interface and same_family
                direct_imports = {
                    primary + name if name.startswith(":") else name
                    for imported in IMPORT.finditer(unit.masked)
                    for name in [imported.group(1)]
                }
                local_partitions = {
                    name for name, partition in owners.items()
                    if name.startswith(primary + ":") and partition.owner == unit.owner and partition.interface
                }
                imports_owned_partition = bool(direct_imports & local_partitions)
                if not (owned_partition or (same_family and imports_owned_partition)):
                    report(
                        unit,
                        0,
                        "implementation-owner",
                        owners[primary].owner,
                        f"{primary} requires its primary folder or a directly imported private partition owner",
                    )
        for imported in IMPORT.finditer(unit.masked):
            name = imported.group(1)
            if name.startswith(":"):
                if not unit.declaration:
                    report(unit, imported.start(), "relative-import", name, "partition import outside a named module")
                    continue
                name = unit.declaration.split(":", 1)[0] + name
            if name not in owners:
                if name == "phoenix" or name.startswith("phoenix."):
                    report(unit, imported.start(), "unknown-import", name, "no first-party interface owns this import")
                continue
            if ":" in name and unit.declaration.split(":", 1)[0] != name.split(":", 1)[0]:
                report(unit, imported.start(), "partition-import", name, "module partitions are private to their primary module")
            # Implementation-unit imports do not create an interface compile cycle.
            if unit.declaration in graph and (unit.interface or ":" in unit.declaration):
                graph[unit.declaration].add(name)

        includes = []
        for include in LOCAL_INCLUDE.finditer(unit.text):
            hash_position = unit.text.find("#", include.start(), include.end())
            if unit.masked[hash_position] != "#":
                continue  # Text in a comment, string or embedded shader/JS body.
            includes.append((hash_position, include))
        class_includes = inside_class(unit.masked, [position for position, _ in includes])
        for position, include in includes:
            included = (unit.path.parent / include.group(1)).resolve()
            try:
                relative = included.relative_to(root)
            except ValueError:
                # Third-party headers may be found through compiler include paths.
                continue
            target = relative.as_posix()
            if relative.parts and relative.parts[0] == "src":
                if not included.is_file():
                    report(unit, position, "missing-local-include", target, "local first-party include does not exist")
                elif module_folder(relative) != unit.owner:
                    report(unit, position, "cross-module-include", target, "use the owning module's interface")
            if position in class_includes:
                report(unit, position, "class-body-include", target, "compile implementation independently of its enclosing class")

    visiting: list[str] = []
    visited: set[str] = set()
    seen_cycles: set[frozenset[str]] = set()

    def visit(name: str) -> None:
        if name in visiting:
            cycle = visiting[visiting.index(name) :] + [name]
            identity = frozenset(cycle)
            if identity not in seen_cycles:
                seen_cycles.add(identity)
                report(owners[name], 0, "interface-cycle", name, " -> ".join(cycle))
            return
        if name in visited:
            return
        visiting.append(name)
        for dependency in sorted(graph[name]):
            visit(dependency)
        visiting.pop()
        visited.add(name)

    for name in sorted(graph):
        visit(name)
    return sorted(set(diagnostics)), source_count, len(owners)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--exceptions", type=Path, help="exact temporary migration exceptions (JSON version 1)")
    parser.add_argument("--allow-unused-exceptions", action="store_true", help="inventory only; final checks reject stale exceptions")
    parser.add_argument("--json", action="store_true", help="machine-readable diagnostics for migration inventory")
    arguments = parser.parse_args()
    try:
        diagnostics, sources, modules = inspect(arguments.root)
        exceptions = load_exceptions(arguments.exceptions)
        remaining: list[Diagnostic] = []
        used: set[tuple[str, str, str]] = set()
        allowed = {(entry["source"], entry["rule"], entry["target"]) for entry in exceptions}
        for diagnostic in diagnostics:
            identity = (diagnostic.source, diagnostic.rule, diagnostic.target)
            if identity in allowed:
                used.add(identity)
            else:
                remaining.append(diagnostic)
        if not arguments.allow_unused_exceptions:
            for source, rule, target in sorted(allowed - used):
                remaining.append(Diagnostic(source, 1, "unused-exception", target, f"remove stale {rule} exception"))
        if arguments.json:
            print(json.dumps({"sources": sources, "modules": modules, "exceptions_used": len(used), "diagnostics": [asdict(item) for item in remaining]}, indent=2))
        else:
            for diagnostic in remaining:
                print(f"{diagnostic.source}:{diagnostic.line}: {diagnostic.rule}: {diagnostic.message} [{diagnostic.target}]")
            print(f"Module ownership: {sources} C++ sources, {modules} module units, {len(used)} temporary exceptions, {len(remaining)} violations")
        return 1 if remaining else 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Module ownership check failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
