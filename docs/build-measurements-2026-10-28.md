# Development build measurements — 28 October 2026

These are manually requested local iteration measurements, not CI timing thresholds or runtime-performance evidence. The strongest findings are the expensive threaded-web link and the broad recompilation caused by World’s private runtime interface.

The measured incremental cases used revision `90ceb3b22125fe7fcc56fb2ffa93a8facbc6ef1c`, Windows 11, an Intel Core Ultra 9 275HX (24 logical processors), 64 GB RAM, CMake 3.31.6, MSVC 19.38.33145 and Emscripten 6.0.12/Clang 24. Native used Visual Studio 2022/MSBuild; web used Ninja and the already-configured threaded runtime. Both used Release and eight build workers. Native C++ flags included `/O2 /Ob2 /DNDEBUG`; web used `-O3 -DNDEBUG` with WebAssembly exceptions and pthreads. Dependency batching remained enabled.

Commands measured the complete wrapper invocation, including startup, dependency scanning, compilation, linking and any ordinary asset work:

```powershell
./scripts/build.ps1 -Target native -BuildTarget native -Jobs 8
./scripts/build.ps1 -Target web -BuildTarget server -Jobs 8
./scripts/build.ps1 -Target web -BuildTarget client -Jobs 8
```

After bringing the existing trees up to date, the implementation case appended a temporary comment to `src/world/lifecycle/queries.cpp` for each of three trials. The shared-interface case appended a comment to `src/world/lifecycle/runtime.cppm`; removing that comment and rebuilding supplied its second sample. These edits forced ordinary production build paths without changing behavior. Each source file was restored from its original byte array. Native object counts came from `.obj` modification times within each timed invocation; web counts came from Ninja’s object-compilation steps.

| Case                                             | Native `native`          | Threaded web `server`    | Compiled objects per run |
| ------------------------------------------------ | ------------------------ | ------------------------ | ------------------------ |
| Warm no-op                                       | 12.049, 12.729 s         | 1.377, 1.396, 1.623 s    | 0                        |
| Implementation-only edit                         | 15.660, 16.191, 18.067 s | 55.594, 57.332, 59.704 s | 1                        |
| Private World runtime interface edit/restoration | 105.247, 128.911 s       | 536.292, 564.904 s       | 102                      |

The web client’s three no-op runs took 1.048, 1.088 and 1.193 seconds. The implementation-only medians were **16.191 seconds native** and **57.332 seconds web**.

The first native no-op candidate took 43.227 seconds, but it was excluded: a concurrent documentation commit changed `.git/refs/heads/main`, which SDL tracks for its generated revision information. That invocation reran CMake and rebuilt `SDL.obj`. The two subsequent native no-op runs compiled nothing. Initial warm-ups also rebuilt stale outputs and packs and are not clean-build comparisons: native took 440.144 seconds, web server 860.613 seconds, and web client 39.177 seconds. Those warm-ups spanned earlier worktree revisions. An earlier MSBuild attempt stalled inside the restricted process sandbox and was excluded; successful native timings used execution outside that sandbox.

Ordinary desktop applications remained open. A three-second CPU sample found unrelated ChatGPT and SmartGit activity alongside Clang; a memory sample during compilation showed approximately 39 GB available. Runs were sequential, without deliberately overlapping benchmarks. The ranges describe this development machine/session, not guaranteed build times.

For the first web implementation edit, Ninja recorded the changed object complete at 9.248 seconds, the static archive complete at 9.700 seconds, and the executable link complete at 58.865 seconds. Thus the executable link alone took **49.165 seconds**. The third run’s link took 47.355 seconds. Most of the wait after an ordinary implementation edit was linking rather than compiling that file.

The first web shared-runtime build compiled 102 objects and reached the static archive at 487.260 seconds. Its detailed `.ninja_log` records show `runtime.cppm` taking **120.725 seconds** and the dependent `effect-work/integration.cppm` taking **146.249 seconds**. The restoration run took 118.182 seconds for `runtime.cppm` and reached the archive at 507.549 seconds. These are individual command durations, not differences between adjacent progress messages: parallel commands overlap. The shared partition is private, so this result measures World’s implementation dependency closure rather than recompilation of every application caller.

For the link-only comparison, Ninja preparation retained the server response file, compiling the server entry point once and linking; this preparation was excluded. The generated server link command was then replayed three times at each optimisation level, changing only `-O3` to `-O0`. Source objects retained their Release `-O3` compilation. Trial order was O3/O0, O0/O3, O3/O0, followed by an ordinary O3 link to restore the normal Release output.

| Server link setting         | Individual runs          | Median   |
| --------------------------- | ------------------------ | -------- |
| Existing `-O3`              | 48.142, 51.037, 54.597 s | 51.037 s |
| Development candidate `-O0` | 1.873, 2.194, 2.373 s    | 2.194 s  |

The median link-only reduction was **48.843 seconds / 95.7%**, approximately **23 times faster**. A development link option is therefore the clearest inexpensive improvement for ordinary web edits. This comparison leaves C++ compilation optimised; it does not measure an entire implementation-edit cycle with the new link setting.

All completed timed builds and link comparisons exited successfully. Both benchmarked source files were restored, with `git diff --exit-code` confirming no remaining change to either file. The final normal `-O3` server link also succeeded. A subsequent final no-op verification attempt was cancelled and excluded when unrelated edits to `contracts.cppm`, `spatial.cppm` and the menu appeared at 20:46 and triggered broad native recompilation. Those edits were preserved. Consequently this report does not claim that the later, concurrently edited worktree has passed a complete build or system gate.

These observations support three concrete next steps:

1. Add an opt-in development web link setting. The isolated link comparison tests this directly; keep Release linking for delivery and performance checks.
2. Reduce World runtime compilation cost and dependency breadth. Examine exported inline definitions and the expensive dependent partitions, with repeated measurements after each change. Merely moving files into different folders would preserve the measured dependency closure.
3. Supply SDL’s pinned revision explicitly. The pinned SDL CMake code accepts an `SDL_REVISION` cache string when `REVISION.txt` is absent, avoiding its fallback Git discovery. The current fallback tracks Phoenix’s revision and caused a measurable regeneration/recompile after a documentation commit. This override has been identified, not implemented or benchmarked here.

The 12-second native no-op also warrants a separate MSVC/Ninja comparison. Neither a generator switch, a different worker count nor lower C++ compilation optimisation was benchmarked in this session. Link timings do not establish runtime speed, download size or gameplay performance for a development build.

Raw results, logs, object lists and the local measurement scripts are retained under ignored `build/compile-measurements-2026-10-28/`, `build/measure-compile.ps1`, `build/measure-link.ps1` and `build/verify-compile-measurements.ps1`. They are not mandatory tests. No production tuning, compiler settings or application behavior is intentionally changed by these measurements.
