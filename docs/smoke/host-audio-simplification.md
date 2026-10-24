# Host audio simplification — 24 October 2026

The four review follow-ups reuse the existing presentation/execution systems:

- The renderer supplies visible source identities using its prepared camera, sprite bounds (including attachments) or cached mesh bounds, and projection. The separate audio frustum and aspect cache are removed.
- GameScreen builds one scene snapshot per frame and shares it with drawing and audio. Host preparation continues on local Consoles; cockpit hull hiding retains own-ship feedback. Sound-only effects use the renderer's projection too.
- `firing_audio` belongs to directed operating modes. Existing authoring, validation, dependency export and selected-mode execution supply cannon/beam clips. Beam loops share the existing sustained visual-execution predicate. Maintained production/development mappings are migrated without gameplay tuning changes.
- Music and SFX loops use one memory-owning stream wrapper. Host audio, music and siren invalidation share one session/generation boundary. Existing mixer gains, event deduplication, voice bound and recovery behavior remain.

The implementation commit is `140c21e`. Verification uses `3c76801` plus this change in `build/audio-source`, excluding concurrent working changes. Source matches the owned working files byte-for-byte; a structural comparison confirms the content migration changes only firing-audio placement. V2 and recording/provenance files are untouched.

## Checks

Six native integration cases pass: directed, combat, input-and-semantics, content-and-packs, local-session and ship-camera. The combat case additionally switches one fitting between launch/WAV and beam/OGG modes, waits for its committed launch cycle, then checks beam-loop removal on switching back. Existing refused-fire, power interruption, shield aggregation, actual thrust, visibility/deduplication, remote pack isolation, Workshop round-trip and recovery checks pass. Production, Test Missions, test and test-3d packs package and verify. All 30 launcher checks pass. Changed C++ and documentation formatting checks pass.

## Native output smoke

The explicit `phoenix_combat_smoke <pack-directory> audio` run passes on Windows/raylib/WASAPI. Synthetic renderer inputs check front/behind/outside sources, camera movement, cockpit hiding and sound-only effects. Six ordinary weapon commands are admitted in the maintained Courier duel setup. Diagnostics confirm interior, engine, shield, cutting-beam and cannon assets; red-alert music returns to ordinary mission music on exit.

| Phase                          | Nonzero / total samples | Peak before Master |
| ------------------------------ | ----------------------: | -----------------: |
| SFX                            |         634549 / 634560 |             1.5890 |
| Muted                          |           1919 / 250560 |             0.1746 |
| SFX on local Console           |         859037 / 864000 |             0.6364 |
| Red-alert Music                |         543948 / 544320 |             0.1408 |
| Muted, ordinary music restored |           3839 / 251520 |             0.0446 |

Mute transitions retain a short queued-buffer tail. Output statistics establish stream/device activity, not a human listening judgment. V2 gains remain unchanged; full-volume effects can exceed unity before the device mix, as recorded in the original [audio receipt](host-audio.md).

## Browser

Native `native` and browser `server`, `client` and `workshop` roles build successfully. All six WASM cases pass: combat, directed, content, local-session, input-and-semantics and ship-camera. An explicit Chrome 155.0.8059.85/SwiftShader run hosts Asteroid duel with a Courier through the ordinary launcher and local `/phoenix` signalling service. Initial asset loading is slow and temporarily blocks DOM inspection; loading completes and the mission runs. The two live AudioContexts reach `running` after normal interaction. Real Web Audio processor measurements follow one second of settling and approximately two seconds of capture per phase. Music/Siren are zero for SFX; SFX/Siren are zero for Music.

| Phase        | Nonzero / total samples |   Peak |
| ------------ | ----------------------: | -----: |
| sfx          |         200668 / 200704 | 0.4003 |
| muted        |              0 / 192512 | 0.0000 |
| music        |         196479 / 196608 | 0.1922 |
| master-muted |              0 / 192512 | 0.0000 |
| restored     |         204645 / 204800 | 0.0610 |

All four accessible Settings sliders return to 50% and output resumes. **Quit to Main Menu** ends the mission and returns to the launcher. This run checks browser unlock, continuous playback, Settings navigation, mixer muting and departure. It does not separately command the browser cutting beam, red-alert transition or shield spike; native smoke and synthetic native/WASM cases cover those paths.

Human listening, loop-seam/fatigue and full-volume balance judgment, physical six-display routing and assistive-technology device acceptance remain unrun.
