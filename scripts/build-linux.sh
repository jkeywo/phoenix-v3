#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"
# Use the project-pinned SDK's native Clang and matching dependency scanner.
# Clang 19 crashes while emitting some of the imported World module types.
# CC/CXX may select another supported Clang version on the first configuration.
compiler_directory="$repo/.tools/emsdk/upstream/bin"
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CC:-$compiler_directory/clang}" -DCMAKE_CXX_COMPILER="${CXX:-$compiler_directory/clang++}" \
    -DGLFW_BUILD_WAYLAND=OFF
if (( $# )); then
    cmake --build build/linux --parallel "${JOBS:-4}" --target "$@"
else
    cmake --build build/linux --parallel "${JOBS:-4}"
fi
