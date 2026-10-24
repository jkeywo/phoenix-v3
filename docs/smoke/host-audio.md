# Host audio reuse — 24 October 2026

This increment promotes the six requested recordings into tracked production and development pack assets. V2 and V3 raw originals were read only. Each pack records source/output SHA-256 hashes and the existing provenance; conversion is not part of the build. The initial gains and shield envelope follow V2.

## Automated verification

Verification uses an isolated copy of committed V3 source plus the audio changes, excluding concurrent Console-layout and rescue editing work. The compiled baseline is `5143498` plus this audio increment, including the directed-system implementation. The audio commit is `b041997`; production content verification was also repeated successfully after the concurrent authored-layout content migration. Tests use synthetic content loaded through the real pack loader and simulation.

The combat integration exercises successful and refused cannon execution, a funded beam interrupted by exhausted power and by disablement, actual engine output, real shield resolution, aggregation of multiple shield contributions, local hit spikes, camera visibility, event deduplication, pause/reset and save recovery without historical receipts. A real PDC interception case verifies that missiles and PDCs acquire no generic cannon cue. Workshop round-trip retains ship, weapon and shield audio metadata and assets; scenario closure includes the recordings while remote Console bundles exclude them. Corrupt audio is rejected without opening a device.

Seven native cases pass: combat, content-and-packs, local-session, network-content, persistent-content, file-imports and input-and-semantics. Five WASM cases pass: combat, content, local-session, network-content and file imports. All 29 launcher checks pass. Production, Test Missions and both independent development roots package and verify; original/runtime recording hashes match all three provenance manifests. A structural comparison confirms no gameplay tuning changed.

Related content, portable-session, network-content, persistent-content and file-import checks cover the 64 MiB pack archive allowance. Expanded source content remains limited to 64 MiB; saves retain their independent limits. A synthetic portable save embeds packs larger than the previous 32 MiB bound and restores them through the production chunk path.

## Explicit native device smoke

`phoenix_combat_smoke <pack-directory> audio` uses the real authored Courier and pirate fittings, ordinary admitted Console commands and the production presenter. Its temporary in-memory scenario places the combatants together; it changes no shipped gameplay tuning. It loads ambience, engine, shield, cutting-beam and cannon recordings through raylib/WASAPI. The five phases alternate SFX output, silence, SFX output, red-alert Music output and silence. The third phase remains on a local Console, with continuous SFX output. The final phase leaves red alert; diagnostics confirm `the_final_battle.ogg` is replaced by ordinary `ambient.ogg`, and the final board is condition 5. Six weapon commands are admitted.

A mixed-audio processor records sample counts and peak amplitude; queued buffers can leave a short tail on a mute transition. The final run records:

| Phase                          | Nonzero / total samples | Peak before Master |
| ------------------------------ | ----------------------: | -----------------: |
| SFX                            |         864923 / 864960 |             1.5469 |
| Muted                          |           3840 / 342720 |             0.1017 |
| SFX on local Console           |         966528 / 970560 |             0.6257 |
| Red-alert Music                |         661734 / 662400 |             0.0776 |
| Muted, ordinary music restored |           4791 / 315840 |             0.0080 |

The processor observes audio before device Master gain. Its measurements establish decoder/stream/output activity, not what a person heard. Full-scale overlapping effects can exceed unity before the final device mix; this increment retains the requested V2 gains. Default Master/SFX settings reduce that level, but full-volume balance needs listening acceptance.

## Browser and outstanding acceptance

The server, client and Workshop WASM roles build, and the five browser integration cases above pass. An explicit Chrome 155.0.8059.85/SwiftShader run hosts the maintained Asteroid duel with a Courier through the ordinary launcher and local `/phoenix` signalling service. Both live AudioContexts reach `running` after normal UI interaction. Diagnostics confirm interior, engine and shield streams, ordinary mission music and a cannon sample. The engine is correctly quiet while the ship has no actual thrust.

Output is measured from the real Web Audio processor after its game callback, while using the accessible Settings sliders. Music and Siren are zero in the SFX phase; SFX and Siren are zero in the Music phase. After a one-second settling period, each measurement covers approximately two seconds:

| Phase        | Nonzero / total samples |   Peak |
| ------------ | ----------------------: | -----: |
| sfx          |         200648 / 200704 | 0.0599 |
| muted        |              0 / 196608 | 0.0000 |
| music        |         196517 / 196608 | 0.1239 |
| master-muted |              0 / 196608 | 0.0000 |
| restored     |         196502 / 196608 | 0.0586 |

All four original volume settings are restored to 50%, and output resumes. Leaving through **Quit to Main Menu** ends the mission presentation. These are signal/output checks, not a human listening judgement. The browser run does not separately command the cutting beam, red-alert transition or a shield spike; those paths are covered by the native smoke and synthetic WASM integration, and separate browser listening acceptance remains pending.

Human speaker/headphone listening, loop-seam/fatigue judgement, full-volume balance, physical six-display routing and assistive-technology device acceptance remain unrun. Automated signal activity, successful builds and a rendered window do not establish those checks.
