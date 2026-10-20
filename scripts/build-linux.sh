#!/usr/bin/env bash
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo"
# C++20 named modules require Clang's matching dependency scanner and Ninja.
# CC/CXX may select another supported Clang version on the first configuration.
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="${CC:-clang-19}" -DCMAKE_CXX_COMPILER="${CXX:-clang++-19}" \
    -DGLFW_BUILD_WAYLAND=OFF
if (( $# )); then
    cmake --build build/linux --parallel "${JOBS:-4}" --target "$@"
else
    cmake --build build/linux --parallel "${JOBS:-4}"
fi
