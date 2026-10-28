#!/usr/bin/env python3
"""Exercise architecture refusal through the production source/CMake checkers."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts/check-module-boundaries.py"


def write(root: Path, name: str, content: str) -> None:
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def source_check(root: Path, expected: set[str], *arguments: str) -> dict:
    result = subprocess.run(
        [sys.executable, str(CHECKER), "--root", str(root), "--json", *arguments],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode not in (0, 1):
        raise RuntimeError(result.stderr or result.stdout)
    value = json.loads(result.stdout)
    actual = {entry["rule"] for entry in value["diagnostics"]}
    if actual != expected or result.returncode != bool(expected):
        raise AssertionError(f"Expected {expected}, got {actual}: {result.stdout}")
    return value


def exercise_sources(root: Path) -> None:
    write(root, "src/base/data/data.cppm", "export module phoenix.data;\nexport struct Data {};\n")
    write(root, "src/ui/model/model.cppm", "export module phoenix.model;\nimport phoenix.data;\n")
    write(root, "src/ui/model/detail.hpp", "struct Detail {};\n")
    write(
        root,
        "src/ui/model/model.cpp",
        '''module;
#include <vector>
module phoenix.model;
import phoenix.data;
#include "detail.hpp"
const char* shader = R"shader(
export module phoenix.fake;
import phoenix.hidden;
class Fake {
#include "hidden.hpp"
};
)shader";
const auto budget = 1'000;
// import phoenix.fake;
/* export module phoenix.also_fake; */
''',
    )
    # Implementation dependency back to an importing interface is legal: only
    # interface imports participate in the interface compilation DAG.
    write(root, "src/base/data/data.cpp", "module phoenix.data;\nimport phoenix.model;\n")
    # Both exported and private implementation partitions retain primary ownership.
    write(root, "src/ui/model/part.cppm", "export module phoenix.model:detail;\nimport phoenix.data;\n")
    write(root, "src/ui/model/private.cpp", "module phoenix.model:internal;\nimport phoenix.data;\n")
    write(root, "src/ui/model/partition.cpp", "module phoenix.model;\nimport :detail;\nimport :internal;\n")
    source_check(root, set())

    # Named private interface partitions may own another composition folder in
    # the same family. An ordinary unit must directly import its local owner.
    write(root, "src/ui/bindings/bindings.cppm", "export module phoenix.model:bindings;\nimport :detail;\n")
    write(root, "src/ui/bindings/adapter.cpp", "module phoenix.model;\nimport :bindings;\n")
    source_check(root, set())
    write(root, "src/ui/bindings/adapter.cpp", "module phoenix.model;\nimport phoenix.model:bindings;\n")
    source_check(root, set())
    write(root, "src/ui/bindings/adapter.cpp", "module phoenix.model;\nimport :detail;\n")
    source_check(root, {"implementation-owner"})
    write(root, "src/ui/bindings/adapter.cpp", "module phoenix.model;\nimport :bindings;\n")
    write(root, "src/base/data/escape.cppm", "export module phoenix.model:escape;\nimport :detail;\n")
    source_check(root, {"implementation-owner"})
    (root / "src/base/data/escape.cppm").unlink()
    write(root, "src/base/data/escape.cpp", "module phoenix.model;\nimport :bindings;\n")
    source_check(root, {"implementation-owner"})
    (root / "src/base/data/escape.cpp").unlink()

    write(root, "src/base/data/partition.cpp", "module phoenix.data;\nimport phoenix.model:detail;\n")
    source_check(root, {"partition-import"})
    (root / "src/base/data/partition.cpp").unlink()

    write(root, "src/ui/model/broken.cpp", 'module phoenix.model;\nstruct Broken {\n#include "../../base/data/private.hpp"\n};\nimport phoenix.unknown;\n')
    write(root, "src/base/data/private.hpp", "struct Private {};\n")
    value = source_check(root, {"class-body-include", "cross-module-include", "unknown-import"})
    exceptions = [
        {"source": item["source"], "rule": item["rule"], "target": item["target"], "reason": "Synthetic migration edge", "chunk": "fixture"}
        for item in value["diagnostics"]
    ]
    write(root, "exceptions.json", json.dumps({"version": 1, "exceptions": exceptions}))
    source_check(root, set(), "--exceptions", str(root / "exceptions.json"))
    (root / "src/ui/model/broken.cpp").unlink()
    source_check(root, {"unused-exception"}, "--exceptions", str(root / "exceptions.json"))

    write(root, "src/base/data/data.cppm", "export module phoenix.data;\nimport phoenix.model;\n")
    source_check(root, {"interface-cycle"})
    write(root, "src/base/data/data.cppm", "export module phoenix.data;\n")
    write(root, "src/ui/model/copy.cppm", "export module phoenix.data;\n")
    source_check(root, {"duplicate-module", "implementation-owner"})
    (root / "src/ui/model/copy.cppm").unlink()
    invalid = root / "src/ui/model/invalid.cpp"
    invalid.write_bytes(b"// invalid non-UTF-8 comment \x96\n")
    source_check(root, {"source-encoding"})
    invalid.unlink()
    write(root, "src/bad.cpp", "int main() {}\n")
    source_check(root, {"folder-layout"})


def exercise_cmake(root: Path, cmake: str) -> None:
    fixture = root / "cmake-fixture"
    boundary_file = (ROOT / "cmake/Boundaries.cmake").as_posix()
    common = f'''cmake_minimum_required(VERSION 3.28)
project(BoundaryFixture LANGUAGES NONE)
include("{boundary_file}")
add_library(frontend INTERFACE)
add_library(adapter INTERFACE)
add_library(authority INTERFACE)
add_library(Authority::Server ALIAS authority)
target_link_libraries(frontend INTERFACE adapter)
'''

    def configure(body: str, expected: str = "") -> None:
        write(fixture, "CMakeLists.txt", common + body)
        result = subprocess.run(
            [cmake, "-S", str(fixture), "-B", str(root / "cmake-build")],
            capture_output=True,
            text=True,
            check=False,
        )
        output = result.stdout + result.stderr
        if expected:
            if result.returncode == 0 or expected not in output:
                raise AssertionError(f"Expected configure refusal '{expected}':\n{output}")
        elif result.returncode:
            raise AssertionError(f"Valid dependency configure failed:\n{output}")

    configure('''phoenix_module_dependencies(frontend ALLOW adapter)
phoenix_module_dependencies(adapter)
phoenix_validate_module_dependencies()
phoenix_forbid_dependency(frontend authority "")
''')
    # A forbidden alias behind a non-selected configuration must not hide from
    # the policy. This fixture requires no compiler, SDK, socket or graphics.
    configure('''target_link_libraries(adapter INTERFACE "$<$<CONFIG:Debug>:$<LINK_ONLY:Authority::Server>>")
phoenix_forbid_dependency(frontend authority "")
''', "Forbidden link dependency")
    configure('''target_link_libraries(adapter INTERFACE "$<IF:$<BOOL:1>,authority,frontend>")
phoenix_module_dependencies(frontend ALLOW adapter)
phoenix_module_dependencies(adapter)
phoenix_validate_module_dependencies()
''', "Undeclared module dependency")
    configure('''target_link_libraries(adapter INTERFACE Authority::Server)
phoenix_module_dependencies(frontend ALLOW adapter)
phoenix_module_dependencies(adapter ALLOW Authority::Server)
phoenix_validate_module_dependencies()
''')
    # A layered diamond is common when many leaf modules share contracts. Each
    # reachable target must be visited once, not once per path through the DAG.
    diamond = '''foreach(level RANGE 0 11)
    add_library(left_${level} INTERFACE)
    add_library(right_${level} INTERFACE)
endforeach()
target_link_libraries(frontend INTERFACE left_0 right_0)
foreach(level RANGE 0 10)
    math(EXPR next "${level} + 1")
    target_link_libraries(left_${level} INTERFACE left_${next} right_${next})
    target_link_libraries(right_${level} INTERFACE left_${next} right_${next})
endforeach()
phoenix_forbid_dependency(frontend authority "")
get_property(visited GLOBAL PROPERTY PHOENIX_BOUNDARY_VISITED)
list(LENGTH visited count)
if(NOT count EQUAL 26)
    message(FATAL_ERROR "Dependency diamond did not visit each target exactly once")
endif()
'''
    configure(diamond)
    # Later configure code may extend the same graph; no previous successful
    # traversal may hide a newly forbidden edge.
    configure(diamond + '''target_link_libraries(right_11 INTERFACE "$<$<CONFIG:Debug>:authority>")
phoenix_forbid_dependency(frontend authority "")
''', "Forbidden link dependency")


    # Platform system-library names are linker inputs, not CMake target edges.
    configure('''target_link_libraries(adapter INTERFACE user32 ole32)
phoenix_module_dependencies(frontend ALLOW adapter)
phoenix_module_dependencies(adapter ALLOW user32 ole32)
phoenix_validate_module_dependencies()
''')
    configure('''phoenix_module_dependencies(adapter ALLOW phoenix_misspelled_owner)
phoenix_validate_module_dependencies()
''', "Allowed dependency of adapter is not a target")
    configure('''target_link_libraries(adapter INTERFACE Authority::Server)
phoenix_forbid_dependencies(frontend "authority;not_present")
''', "Forbidden link dependency")
    # A property alone is sufficient registration, including an intentionally
    # empty set. Omitting the property must never exempt a new module target.
    write(fixture, "src/new-owner/CMakeLists.txt", "add_library(phoenix_new_owner INTERFACE)\n")
    configure('''add_subdirectory(src/new-owner)
phoenix_require_module_policies("${PROJECT_SOURCE_DIR}")
''', "Module target has no intentional dependency policy")
    configure('''add_subdirectory(src/new-owner)
set_property(TARGET phoenix_new_owner PROPERTY PHOENIX_ALLOWED_DEPENDENCIES "")
phoenix_require_module_policies("${PROJECT_SOURCE_DIR}")
phoenix_validate_module_dependencies()
''')
    configure('''add_subdirectory(src/new-owner)
set_property(TARGET phoenix_new_owner PROPERTY PHOENIX_ALLOWED_DEPENDENCIES "")
target_link_libraries(phoenix_new_owner INTERFACE adapter)
phoenix_require_module_policies("${PROJECT_SOURCE_DIR}")
phoenix_validate_module_dependencies()
''', "Undeclared module dependency")
    # Use empty synthetic owners to exercise the production final-policy hook.
    # This creates names, never allows actual edges from observed links.
    policy_file = ROOT / "cmake/ModulePolicy.cmake"
    import re
    target_names = sorted(set(re.findall(r"phoenix_[A-Za-z0-9_]+|RmlUi::RmlUi|nlohmann_json::nlohmann_json|raylib|webp|flecs_static", policy_file.read_text())))
    empty_owners = "".join(
        f"add_library({name} INTERFACE" + (" IMPORTED" if "::" in name else "") + ")\n"
        for name in target_names
    )
    include_policy = f'include("{policy_file.as_posix()}")\n'
    configure(empty_owners + include_policy)
    # Even an explicitly allowed late link into a native leaf must fail the
    # independent frontend role guard at the end of the root directory.
    configure(empty_owners + '''phoenix_module_dependencies(phoenix_network_client ALLOW phoenix_gameplay_flight)
''' + include_policy + '''target_link_libraries(phoenix_network_client INTERFACE phoenix_gameplay_flight)
''', "Forbidden link dependency")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default=shutil.which("cmake"))
    arguments = parser.parse_args()
    cmake = arguments.cmake
    if not cmake:
        bundled = ROOT / ".tools/python/cmake/data/bin/cmake.exe"
        if bundled.is_file():
            cmake = str(bundled)
    if not cmake:
        parser.error("CMake is required; pass --cmake or use the repository bootstrap")
    # Keep generated fixtures inside the workspace, including restricted Windows
    # environments whose redirected OS temporary directory is not writable.
    fixture_parent = ROOT / "build"
    fixture_parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="module-boundaries-", dir=fixture_parent) as temporary:
        root = Path(temporary)
        exercise_sources(root)
        exercise_cmake(root, cmake)
    print("PASS module ownership, explicit migration exceptions and conditional/transitive dependency refusal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
