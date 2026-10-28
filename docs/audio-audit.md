# Music and sound audit

24 October 2026. Repository inspection of the current V3 working tree and read-only inspection of `C:/Coding/project-phoenix-v2`. This is an inventory and proposed work order, not an implementation or listening acceptance record. Existing unrelated working changes were left alone.

## Implementation follow-up

The requested reuse increment adds `the_final_battle` for red alert; host-ship engine/interior ambience; committed main-cannon fire; the Courier cutting beam; and effect-owned shield beds/hit spikes. PDCs, missiles and other beam families retain their previous sound coverage. See [host audio behavior and authoring](audio.md) for the current contract and [host audio verification](smoke/host-audio.md) for evidence. The inventory and gap analysis below describe the repository at the start of this audit. The follow-up simplification uses the renderer’s shared scene/culling result, directed-mode audio metadata, the existing sustained execution predicate, and a shared stream owner/session reset. See the [follow-up verification](smoke/host-audio-simplification.md).

## What V3 had at audit start

28 October follow-up: the six selected CC0 audition recordings now cover committed PDC fire, FTL reinforcement arrivals/departures, delivered Comms attention, critical ship status and funded psychic activation. Deleted candidates and Kenney packs are excluded. See [selected audio bindings](selected-audio.md) for triggers, provenance and remaining acceptance; the inventory below remains the historical audit.

There are **five unique shipped recordings**: two music tracks and three effects. Seven audio files exist under `content/` because the red-alert siren is duplicated in the two development bases. The siren copies are byte-identical. There is no broader weapon, machinery or notification library hidden in the tracked content.

| Recording                                       | Current use                                                                  | Evidence / limitations                                                                                                                                           |
| ----------------------------------------------- | ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `content/audio/menu.ogg`                        | Looped native/server preparation music                                       | Tragic ambient main menu, credited CC0 in `content/audio/CREDITS.txt`. The HTML pre-launch menu plays clicks but has no music player.                            |
| `content/audio/ambient.ogg`                     | Looped mission music, including fallback when a condition has no music       | Observing the Star, credited CC0 in the same notice. This is music, not a separate ship-machinery ambience layer.                                                |
| `content/audio/ui_click.ogg`                    | Routine interface activation in HTML and native/WASM UI                      | Copied unchanged from V2. Eight local voices, with 0.94–1.06 pitch variation. No distinct refusal, timeout or actionable-message tone.                           |
| `content/base/assets/audio/red_alert_siren.ogg` | Once on entry to condition 1; copies in `content/test` and `content/test-3d` | Copied unchanged from V2. Initial snapshots and restored sessions suppress historical sirens. Other authored levels have no siren.                               |
| `content/base/assets/combat/explosion.wav`      | Authored destruction/blast effects, including ship and ordnance definitions  | Original procedural V3 recording according to `assets/combat/CREDITS.txt`. A generic explosion does not supply firing, shield-hit or ordinary hull-hit feedback. |

`raw/audio/the_final_battle.ogg` (2,822,466 bytes) is present locally but ignored, unbundled and unused. The other two raw recordings are byte-identical sources of the shipped menu and mission tracks. The combat recording's creator/source/licence is not recorded in the inspected audio credits; recover that information before promotion. Builds must continue to use tracked content rather than depend on `raw/`.

## Playback support versus authored content

The player in `src/presentation/music.cpp` already provides:

- Streaming, looping OGG music, selected from a ship's alert condition with ordinary mission music as fallback. No shipped definition currently supplies a `music` field, so all alert levels retain the same mission track.
- Independent Master, Music, Siren and SFX settings. Music and shared effects play on the ship server presentation; remote Console/GM clients do not duplicate them. Local UI clicks are available independently.
- Once-only live siren transitions and recent, deduplicated blast sounds, with initial/recovered historical events suppressed.
- Operation without a usable audio device. Existing smoke records establish selected loading and interaction behavior; they do not establish how the mix sounds to a listener.

The current boundary is narrow: alert music/sirens and destruction sounds have authoring support; general gameplay cues and continuous machinery loops do not. Combat decoding is hardcoded to WAV; condition music and sirens use OGG. V2's MP3 recordings need an explicit decoder path or conversion to the appropriate tracked format. The music player stops/replaces streams immediately; it has no crossfade or alert ducking. Combat playback has no distance attenuation/panning, and does not use the eight-voice UI pool.

The alert and destruction loaders check that referenced assets exist. They do not establish that the bytes decode as the expected recording type. A bad existing condition asset can fail to play rather than fall back after a decode failure. Add decode diagnostics and a real fallback as part of audio work.

## What is missing, and what V2 can supply

The GDD explicitly requires condition-specific music, an entry siren and distinct non-voice cues with accessible equivalents. The remaining cue families below are proposed presentation coverage for existing gameplay, not newly discovered mandatory GDD mechanics. Avoid sounding every routine simulation change.

V2 filenames below are relative to `C:/Coding/project-phoenix-v2/`. Candidate means the file exists and its historical use is evidenced; it does not mean it has been auditioned or cleared for promotion.

| Gap                                                          | Reusable V2 candidate                                                                                                                                   | Remaining V3 work                                                                                                                                                                                                                                                      |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Combat / high-alert music                                    | `assets/sounds/last_stand_in_space_looped.ogg` (736,088 bytes), selected by V2's world red-alert configuration                                          | Best direct integration candidate: tracked OGG can use the existing condition-music path. Audition against Settled Dark and check provenance. Compare with V3's unused `the_final_battle.ogg`. Decide which alert levels should actually change music.                 |
| Additional exploration music                                 | `assets/sounds/exploration.mp3` (5,070,109 bytes), used by V2's host menu; also `raw/audio/underwater_exploration.mp3` (5,300,196 bytes)                | Optional variety. Existing V3 music already covers the basic role. Convert/package only after audition and provenance review.                                                                                                                                          |
| Ship interior ambience                                       | `assets/sounds/Ambient.mp3` (620,256 bytes), authored at low volume on V2 ships                                                                         | Add a separately controllable ambience layer; check loop seam and fatigue over a 45–90 minute session.                                                                                                                                                                 |
| Thrust / engine load                                         | `assets/sounds/Engine.mp3` (51,120 bytes); raw alternatives `starship_ion_drive_h_#4-1779535370193.wav` and `starship_ion_drive,__#3-1778571964213.mp3` | V2 ship authoring includes idle and thrust-dependent volume. Reimplement a smoothed local loop using permitted actual thrust, with proper stop/pause/session lifecycle.                                                                                                |
| Weapon discharge                                             | `assets/sounds/Blaster.mp3` (18,432 bytes)                                                                                                              | Useful prototype cue, though the name does not establish suitability for V3 kinetic turrets/PDCs. Add admitted fire-event presentation and bounded overlapping voices. Distinct kinetic, missile-launch and PDC families still need selection/design.                  |
| Sustained beam firing                                        | `assets/sounds/PhaserLoop.mp3` (203,256 bytes), authored on V2 Cruiser and larger ships                                                                 | Add per-source start/sustain/stop behavior, including interrupted fire, destruction and session recovery. Filename alone does not prove a clean loop.                                                                                                                  |
| Shield impact                                                | `assets/sounds/ForcefieldHit.mp3` (235,632 bytes)                                                                                                       | V2 uses a quiet bed with damage-driven spikes and decay. Reuse the recording after audition; decide whether V3 wants a bed, short impact or both. Hook to permitted impact facts.                                                                                      |
| Refused command, timeout and actionable private alert        | `assets/sounds/private_refused.wav` (16,364 bytes), `private_timeout.wav` (20,204 bytes), `private_alert.wav` (21,164 bytes)                            | Strongest documented reuse set: generated non-speech tones, with `assets/audio/NOTICE.md`, `scripts/build-private-audio.mjs` and `assets/audio/private-feedback.json`. Route to the relevant local Console, coalesce repetitions and retain visible/localised reasons. |
| Incoming hail, new Comms turn, urgent decision               | `private_alert.wav` can provide an initial generic actionable cue                                                                                       | A recognisable Comms earcon is still desirable. Trigger on a newly actionable permitted conversation/decision, not every transcript refresh. Respect audience and focus.                                                                                               |
| Ordinary hull hit / collision                                | No dedicated recording found in the inspected V2 sound library or raw audio                                                                             | Source or create short material-impact cues. Keep separate from the existing destruction explosion.                                                                                                                                                                    |
| FTL preparation, readiness and departure                     | No dedicated recording found                                                                                                                            | Source/create a small coherent set. Hook to actual readiness/transition results; avoid repeating on restore.                                                                                                                                                           |
| Docking, launch/recovery, scan completion, objective/debrief | No dedicated recordings found; the private actionable tone is a temporary generic option                                                                | Select only useful completion/attention cues. New assets can wait until event routing and notification priorities work.                                                                                                                                                |
| Psychic effects / cloak / wards                              | No dedicated recording found                                                                                                                            | Optional distinctive textures if desired; scope their audibility to known effects and avoid constant warning noise.                                                                                                                                                    |
| Critical damage / heat / power attention                     | Existing red siren and V2 private alert can cover initial attention                                                                                     | Design urgency, thresholds, hysteresis and repetition limits before adding more alarms. Avoid an independent alarm for every System tick.                                                                                                                              |

V2 also has the already-reused `ui_click.ogg` and `red_alert_siren.ogg`. Its raw `scifi_button_click_#2-1778571851542.mp3` is another click candidate, but replacing the working V3 click is low priority. The raw `sci-fi_sounds/` folder contains a licence and web shortcuts, **no recordings**; it is not an available sound library.

## Reusable designs from V2

These are references to behavior and data, not portable C++ modules or evidence of V3 completion:

- `assets/entities/alliance_cruiser.toml`: separate ambience, thrust-driven engine, sustained phaser, positional blaster and damage-responsive shield audio.
- `assets/worlds/default.toml`: once-on-entry alert siren plus looping combat music.
- `assets/audio/private-feedback.json` and `gui/private-audio.js`: outcome-specific local feedback and a short coalescing interval.
- `assets/audio/sound-cues.toml`, `gui/audio-live-equivalents.js` and `gui/sound-audition-panel.js`: category/audience metadata, meaningful text equivalents and author audition controls.
- `gui/audio-ducking.js`, `gui/audio-mix.js` and `gui/audio-range.js`: bounded music suppression around important cues and mix/range policies.

Keep V3 presentation outside authoritative simulation, follow the current Console knowledge model, and reimplement only the behavior needed for the C++/raylib stack. V2's Rust/JavaScript audio system is not a drop-in subsystem.

## Provenance and acceptance

V3's two music tracks have per-recording credits and source links. Its explosion is recorded as original procedural work. The click and siren identify their V2 source but lack upstream creator/source details in those notices.

V2's private-tone notice explicitly records original generation under its repository licence; its `LICENSE` file is AGPL-3.0. The inspected notices/catalogue do not establish individual upstream provenance for the older MP3s or combat OGG. Record creator, source, licence and any conversion alongside promoted assets; do not infer third-party recording rights from a repository-wide software licence. This audit has not independently verified external source licences.

Important cues need localised visible/text and semantic equivalents from the same permitted facts. Shipwide audio should sound once per ship presentation; private Console attention must work for remote players and be deliberately routed on the six-display installation. Human audio/video Comms is a separate missing transport/media feature (`docs/comms-conversations.md` explicitly excludes it from that increment); recordings cannot fill it. AI speech is excluded by the GDD and is not an asset gap.

## Recommended work order

1. **Fill the existing music hook:** audition the two combat candidates, recover provenance, promote one and bind appropriate alert levels. This is the smallest useful audio increment.
2. **Add useful private feedback:** reuse the three generated V2 tones with Console-scoped event routing, visible equivalents and repetition suppression. Include incoming actionable Comms/away decisions.
3. **Make combat audible:** firing, sustained beams and shield hits, then kinetic/PDC/missile distinctions and hull impacts. Reuse V2 recordings where they fit; supply proper loop lifecycle and bounded voice handling.
4. **Add the ship bed:** optional ambience and thrust-driven engines with conservative levels and an ambience control. Keep ordinary crew conversation intelligible; add ducking/crossfades where listening proves useful.
5. **Add sparse milestone cues:** FTL, docking/craft, scan/objective completion and psychic effects, with authored cue IDs and Workshop audition/validation once the common cue path exists.

Verification should check promoted asset/reference validity and decoder diagnostics. Synthetic system integration should exercise real event routing, audience restrictions, repeated updates and restore/session changes without freezing production tuning. Native/browser listening, mix balance, device/browser unlock behavior and multi-Console output need explicit manual smoke evidence. No playback/listening run or runtime build was performed for this audit; checks were source/asset inspection and SHA-256 comparison of reused/source recordings.

## Evidence index

- V3 requirements: `docs/GDD.md` §§3, 7 and 14; `docs/technical-stack.md`.
- V3 implementation: `src/presentation/music.cpp`, `alert_audio.hpp`, `settings.hpp`; `src/content/bridge.cpp`, `combat.cpp`; `src/simulation/combat_ordnance.hpp`; `web/launcher.mjs`; `CMakeLists.txt`.
- V3 authored data: `content/base/definitions.json`, `content/test/definitions.json`, `content/test-3d/definitions.json` and the credit files listed above.
- Existing evidence: `docs/audio.md`, `docs/smoke/menu-audio.md`, `docs/bridge-command.md`, `docs/combat-stage-one.md` and `docs/combat-mvp.md`. Older audio prose omits some later mixer/combat work; source and current content were checked directly.
