# Faster build iteration

The [28 October development measurements](build-measurements-2026-10-28.md) cover the expanded modular codebase: focused no-op builds, implementation edits, World runtime interface changes and a same-object web link optimisation comparison. They identify development web linking and World dependency breadth as the strongest measured opportunities.

[World compilation dependencies](world-build-dependencies.md) records the subsequent Away adapter extraction, native/WASM integration evidence and the measured reduction from 102 affected objects to one for edits to those implementation bodies.

## Development web linking

Use `-DevelopmentLink` to skip expensive executable link optimisation during local web iteration. C++ libraries, named modules and application objects retain their configured Release compilation optimisation. The option covers server, client, Workshop, system fixtures and explicitly selected manual hosts, including hosts with their own link optimisation flags.

```powershell
./scripts/build.ps1 -Target web -BuildTarget client -DevelopmentLink
./scripts/build.ps1 -Target web -BuildTarget server
./scripts/build.ps1 -Target web -BuildTarget server -DevelopmentLink:$false
```

The setting persists in `build/web`: omitting the switch preserves it, while `-DevelopmentLink:$false` restores ordinary Release linking. A fresh build defaults to ordinary linking. Native builds reject the switch. Direct CMake users can set `PHOENIX_WEB_DEVELOPMENT_LINK=ON` or `OFF`. Changing the setting requires relinking, without deliberately rebuilding compiled objects. `scripts/check.ps1` explicitly disables development linking for the complete Release gate and CI/deployment builds.

The same-object threaded server measurement reduced median link time from 51.037 to 2.194 seconds. This option is for iteration; use ordinary Release linking for delivery and runtime-performance measurements. It does not promise a particular runtime speed or download size.

The implementation was verified in an isolated snapshot of `1c423fd` plus the build-option changes, using shared pinned dependency sources and separate build outputs. The threaded client built with development linking and relinked with ordinary optimisation without compiling C++ again; its compiled entry-point hash and 707 captured compiler command lines were unchanged. Generated commands verified the final optimisation flag for server, client, Workshop, a system fixture, the Workshop host and manual mission/profiling hosts. Native wrapper/CMake rejection, the fresh default and caller PATH restoration also passed. Concurrent feature edits in the main worktree were excluded from this validation.

The ordinary [build commands](building.md) configure a new build tree automatically. Subsequent runs use the generated build directly; CMake still regenerates when its tracked inputs change. Use `-Reconfigure` to explicitly configure and build, or `-ConfigureOnly` to configure without compiling. Release settings and the existing native/web output paths are unchanged.

Build just the application or integration case needed for the current edit:

```powershell
./scripts/build.ps1 -Target native -BuildTarget native
./scripts/build.ps1 -Target native -BuildTarget phoenix_simulation_tests
./scripts/build.ps1 -Target web -BuildTarget client
./scripts/build.ps1 -Target web -BuildTarget server,client,workshop
```

CMake builds each selected target's dependencies. Omitting `-BuildTarget` still builds everything; `scripts/check.ps1` retains the complete automatic gate. A focused application build does not build or run the other integration cases. Native's starter archive can be built separately with `-BuildTarget base_pack` (with `test_pack` retained as an alias).

`-Jobs` controls the build worker count, retaining eight as the default. For example, use `-Jobs 4` when memory pressure or other work makes eight unsuitable. The wrapper restores the caller's PATH after a web build, including failed builds.

The base-game ZIP (`base.zip`, with the identical `test.zip` compatibility alias) is a tracked build output. It is regenerated and verified when a source file changes, when files are added or removed, when the pack utility changes, or when the archive is missing. A generated source-list dependency catches removals even when the remaining files are older than the ZIP. Unchanged builds leave the archive untouched.

## Local measurements — 18 October 2026

These observations used the existing Windows Release trees, Visual Studio 2022 and Emscripten/Ninja, with eight workers. They measure the complete `scripts/build.ps1` invocation with no C++ compilation required, including configuration and dependency scanning overhead. Warm-up runs and runs that picked up concurrent source edits were excluded.

| Platform | Previous wrapper       | Updated wrapper      |
| -------- | ---------------------- | -------------------- |
| Native   | 13.79–14.75 s (2 runs) | 2.31–2.57 s (3 runs) |
| Web      | 3.55–3.59 s (2 runs)   | 0.14–0.15 s (3 runs) |

The improvement removes repeated configuration/dependency checks and unnecessary packaging. It does not establish faster clean compiler execution. Selecting a target additionally avoids building unrelated apps and test executables after an edit; the saving depends on that edit's dependency closure. No compiler optimisation, module ownership, runtime behavior or acceptance requirement was changed.

Validation passed the full automatic gate: 11 native system cases, the WASM checks and matching continuation trace, and six network-service cases. Build checks also verified selected targets, worker-count forwarding, failure propagation, environment restoration, explicit configuration, and archive no-op/add/edit/remove behavior. The temporary archive input was removed and the final training ZIP rebuilt without it.

## Clean builds

`cmake/BuildPerformance.cmake` enables two optimisations for the pinned dependencies. Flecs and RmlUi Core compile in batches of eight source files, reducing repeated parsing and compiler startup work. RmlUi's `TemplateCache.cpp` remains separate because it and `StyleSheetSpecification.cpp` define conflicting internal `instance` objects. No vendored source is edited.

For Visual Studio builds, Flecs, SDL, libdatachannel and Mbed TLS crypto also use `/MP4` to compile up to four sources concurrently within each project. `-Jobs` continues to control simultaneous build projects, so it is not a global compiler-process limit. RmlUi retains its upstream parallel-compilation setting; Ninja already schedules individual compilations. First-party named modules retain their existing scanning and compilation rules. Release optimisation and the complete set of applications, probes and tests are unchanged.

The default is `PHOENIX_FAST_DEPENDENCIES=ON`. To restore the previous dependency compilation settings in a configured tree:

```powershell
./.tools/python/cmake/data/bin/cmake.exe -S . -B build/native -DPHOENIX_FAST_DEPENDENCIES=OFF
./.tools/python/cmake/data/bin/cmake.exe -S . -B build/web -DPHOENIX_FAST_DEPENDENCIES=OFF
```

Set it back to `ON` to re-enable the optimisation. Recheck both toolchains when changing the pinned Flecs/RmlUi revisions: source batching can expose internal-name or macro conflicts. See [CMake unity compilation](https://cmake.org/cmake/help/v3.31/prop_tgt/UNITY_BUILD.html) and [MSVC parallel compilation](https://learn.microsoft.com/en-us/cpp/build/reference/mp-build-with-multiple-processes?view=msvc-170).

### Clean-build measurements — 18 October 2026

To avoid concurrent source edits changing the comparison, these builds used an isolated snapshot of `b847f2b` with only this CMake change added. Both configurations built all applications, probes, tests and dependencies in Release using `cmake --build <tree> --config Release --parallel 8 --clean-first`. Timings include cleaning and compilation/linking, but exclude configuration, dependency downloads and SDK installation; downloaded dependency sources and the Emscripten sysroot remain available.

The machine has an Intel Core Ultra 9 275HX with 24 logical processors. Toolchains were MSVC 19.38.33145 with Visual Studio 2022, and Emscripten 6.0.12/Clang 24 with Ninja.

| Platform                  | Previous settings | Fast dependencies | Reduction |
| ------------------------- | ----------------- | ----------------- | --------- |
| Native                    | 145.43 s          | 110.50 s          | 24%       |
| Web, reverse-order repeat | 111.29 s          | 89.27 s           | 20%       |

These are local observations, not CI timing thresholds. The first web pair was 112.70 s / 130.31 s; unchanged application module compilation took up to three times longer during the second run. The reverse-order repeat built the optimised version first, then the baseline, using the same source and worker count. Web build steps fell from 634 to 346. Timing remains sensitive to concurrent machine work; no clean-build speed guarantee is implied.

The isolated optimised builds passed all 11 native system cases, the WASM checks and the native/WASM continuation comparison. Five network-service cases passed on the first run; the live-session case timed out while connecting and passed its focused retry. This evidence covers the fixed snapshot and build change, not concurrent gameplay/presentation edits in the main working tree.
