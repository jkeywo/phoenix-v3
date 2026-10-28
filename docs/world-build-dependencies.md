# World compilation dependencies — 28 October 2026

World’s Away actor/frame implementations now compile outside its shared runtime interface. The earlier [development measurements](build-measurements-2026-10-28.md) showed a shared-runtime edit recompiling 102 objects, taking 105–129 seconds native and 536–565 seconds for the threaded web server.

`src/world/lifecycle/runtime.cppm` no longer defines the eight Away adapter/frame types. Their declarations live in the private `phoenix.simulation:away_adapters` partition, beside their ordinary implementation file, `src/world/crew-bindings/away_adapters.cpp`. The implementation owns 86 constructors, destructors and virtual methods, including 74 function bodies. Only that file and `away_access.cpp`, `away_commands.cpp` and `away_projection.cpp` import the partition. Routine method-body edits therefore leave the interface unchanged; declaration edits reach this small set of consumers.

Ability lifecycle execution is also imported by `:effect_work` instead of reexported through `:runtime`. Generated WASM module maps confirm that `runtime.cppm` imports neither the Away adapter partition nor `phoenix.effects.ability_lifecycle`.

The adapter types moved from nested Runtime declarations into the same private World namespace. Their logical IDs, owner references, cache fields, const/mutable interface separation and non-copyable/non-movable frame rules remain intact. Runtime’s actual data members retain their order. The public World facade, physical command paths, phase order and saved-state format are unchanged. This is a private compilation boundary, not a new domain authority or public API.

The verified source snapshot reduced `runtime.cppm` from 2,262 to 1,828 physical lines. The new declaration partition has 181 lines and its implementation has 442. Across the seven affected C++ files this adds 193 physical lines, principally declarations, qualified names and spacing; it does not duplicate algorithm bodies. A token comparison verified that all 74 moved function bodies preserve the original expressions and string literals, with the equivalent `Impl` alias written explicitly as `Runtime`.

Validation used an isolated snapshot of `1c423fd` plus the [development-link implementation](build-performance.md#development-web-linking) and this refactor, with pinned dependency sources shared and build outputs separate. This avoided interference from concurrent native/WASM builds and feature edits in the live worktree. Both MSVC 19.38 and Emscripten 6.0.12/Clang 24 compiled the final out-of-line implementation. All 13 selected native cases and the same 13 threaded-WASM cases passed: simulation, Away, abilities, processes, missions, medical, security, craft, logistics, flight, swept collision, scenario situations and the disposable Workshop host. Four native/WASM continuation comparisons passed for simulation, Away, abilities and processes. The snapshot’s module ownership check passed with zero exceptions and zero violations.

These checks load synthetic fixtures through production paths. They establish the moved adapters’ behavior and continuation contracts, not graphical, physical-device or runtime-performance acceptance. Concurrent feature edits in the live checkout are outside this validation receipt. General Runtime state/interface changes still have broad consumers; this work specifically removes Away implementation bodies and ability lifecycle execution from that common compilation boundary.

After those checks, a temporary comment was appended to the isolated `away_adapters.cpp`, then the original bytes were restored. Each change built only `phoenix_away_tests` and its dependencies, using eight workers. Every timed build succeeded; no interface objects were compiled and no CMake regeneration occurred.

| Measured build           | Native   | Threaded WASM with development linking | Compiled objects |
| ------------------------ | -------- | -------------------------------------- | ---------------- |
| Warm-up, no source edit  | 19.668 s | 2.327 s                                | 0                |
| Away method-body edit    | 27.745 s | 56.610 s                               | 1                |
| Original source restored | 31.974 s | 31.507 s                               | 1                |

The rebuilt object was `away_adapters.cpp` on both toolchains. Previously these method bodies lived in the common runtime partition, whose edits rebuilt 102 objects. The rebuild-count change directly demonstrates the narrower implementation dependency. These elapsed times are local observations with ordinary desktop activity; the earlier baseline selected native/server applications with ordinary Release linking, while this measurement selected a synthetic integration executable and used development web linking. They are not a controlled application-to-application speedup estimate. Compilation optimisation remained Release; only the development web link setting changed optimisation at link time.

Raw logs, body-comparison evidence, command captures and local validation scripts are retained under ignored `build/iteration-validation/`; the isolated sources/builds are under `build/iter-src/`. These are manually triggered measurements and checks, not new mandatory timing tests or production-content goldens.
