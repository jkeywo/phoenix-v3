# Faster build iteration

The ordinary [build commands](building.md) configure a new build tree automatically. Subsequent runs use the generated build directly; CMake still regenerates when its tracked inputs change. Use `-Reconfigure` to explicitly configure and build, or `-ConfigureOnly` to configure without compiling. Release settings and the existing native/web output paths are unchanged.

Build just the application or integration case needed for the current edit:

```powershell
./scripts/build.ps1 -Target native -BuildTarget native
./scripts/build.ps1 -Target native -BuildTarget phoenix_simulation_tests
./scripts/build.ps1 -Target web -BuildTarget client
./scripts/build.ps1 -Target web -BuildTarget server,client,workshop
```

CMake builds each selected target's dependencies. Omitting `-BuildTarget` still builds everything; `scripts/check.ps1` retains the complete automatic gate. A focused application build does not build or run the other integration cases. Native's starter archive can be built separately with `-BuildTarget training_pack`.

`-Jobs` controls the build worker count, retaining eight as the default. For example, use `-Jobs 4` when memory pressure or other work makes eight unsuitable. The wrapper restores the caller's PATH after a web build, including failed builds.

The training ZIP is now a tracked build output. It is regenerated and verified when a source file changes, when files are added or removed, when the pack utility changes, or when the archive is missing. A generated source-list dependency catches removals even when the remaining files are older than the ZIP. Unchanged builds leave the archive untouched.

## Local measurements — 18 October 2026

These observations used the existing Windows Release trees, Visual Studio 2022 and Emscripten/Ninja, with eight workers. They measure the complete `scripts/build.ps1` invocation with no C++ compilation required, including configuration and dependency scanning overhead. Warm-up runs and runs that picked up concurrent source edits were excluded.

| Platform | Previous wrapper       | Updated wrapper      |
| -------- | ---------------------- | -------------------- |
| Native   | 13.79–14.75 s (2 runs) | 2.31–2.57 s (3 runs) |
| Web      | 3.55–3.59 s (2 runs)   | 0.14–0.15 s (3 runs) |

The improvement removes repeated configuration/dependency checks and unnecessary packaging. It does not establish faster clean compiler execution. Selecting a target additionally avoids building unrelated apps and test executables after an edit; the saving depends on that edit's dependency closure. No compiler optimisation, module ownership, runtime behavior or acceptance requirement was changed.

Validation passed the full automatic gate: 11 native system cases, the WASM checks and matching continuation trace, and six network-service cases. Build checks also verified selected targets, worker-count forwarding, failure propagation, environment restoration, explicit configuration, and archive no-op/add/edit/remove behavior. The temporary archive input was removed and the final training ZIP rebuilt without it.
