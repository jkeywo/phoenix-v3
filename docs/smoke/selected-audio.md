# Selected audio verification

28 October 2026. Selected recordings: Comms attention, critical heat/power/damage warning, FTL arrival and departure, PDC firing and psychic activation. Deleted candidates and Kenney packs are unused.

Recordings/provenance: `0d9c60d`. Wiring, maintained bindings and synthetic checks: `6cdca39`.

## Assets and content

- All six retained raw MP3 hashes match their recorded source hashes. All eighteen promoted PCM16 WAVs match output hashes, sample rates and channel counts.
- Production `base.zip` and independently rebuilt `test.zip` / `test-3d.zip` pass the production pack verifier. Each contains the six WAVs with matching embedded provenance. Raw inputs and conversion tools are not runtime dependencies.
- Synthetic host audio checks exercise the real loader, authored clip validation, Workshop round-trip/export closure and omission of host recordings from remote Console bundles.

## Runtime checks

Native Release `native` builds and `native.exe --build-check` passes. Six relevant native CTests pass: combat, FTL, psychic, conversations, content-and-packs and ship-camera. Four threaded WASM production-path executables build, link and pass under Node: combat, FTL, psychic and conversations. Their native/WASM `--trace` continuations match for all four cases.

The browser server, client and Workshop roles completed an earlier build using Release C++ and the opt-in development linker. A final application refresh after the funding-warning correction is blocked by concurrent, unrelated help UI edits: `src/ui/console-surface/help.cpp:85` calls non-const `GetAbsoluteOffset` / `GetOffsetHeight` through `const Element`. The latest application relink is therefore unverified; the final audio implementation itself compiles, links and passes in the four WASM integration executables. The unrelated UI edits are excluded from the audio commits.

Targeted C++, CMake, JSON and Markdown formatting checks pass. The audio-only production/development authoring snapshots match the verified pack definitions; concurrent production help-text edits are excluded from staging.

The added synthetic production-path assertions cover explicitly authored committed PDC fire, silent unconfigured PDCs/missiles, refused fire, private internal cues, a final shot without interior loops after departure, new versus initial/recovered FTL arrivals, duplicate and refused FTL departure input, delivered versus in-transit Comms, sustained power shortage/coalescing/recovery and funded psychic activation without resume replay. Existing cannon, beam interruption, shields, thrust, visibility and reset assertions remain.

The default all-target build currently reports an unrelated pre-existing module-boundary violation: `tests/system/screen_audio.cpp:2` includes the native audio owner's private header. Application roles and relevant integration targets are built explicitly; that test and its owner are outside this change.

## Device acceptance

New native/browser listening, relative mix balance, actual endpoint routing and browser audio-unlock acceptance have not been run for these six recordings. Decoder, cue routing and build evidence do not establish human listening acceptance. Existing mixer/voice limits and Music/SFX/Master controls are reused without new controls.
