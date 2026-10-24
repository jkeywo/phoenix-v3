# Host music and sound

The native and browser ship host plays one mix for all local display regions. Remote Console/GM clients and Workshop editing do not play the mission recordings. Local Console navigation does not interrupt the host mix. Remote Console asset bundles exclude these recordings.

| Recording              | Authored use                                                                           | Initial gain        |
| ---------------------- | -------------------------------------------------------------------------------------- | ------------------- |
| `the_final_battle.ogg` | Condition 1/red-alert music; other conditions fall back to ordinary mission music      | 0.35 × Music        |
| `ship_ambient.ogg`     | Host ship interior while alive in a running mission                                    | 0.25 × SFX          |
| `engine.ogg`           | Host ship actual funded forward thrust, with local smoothing and no idle level         | 0.15 × thrust × SFX |
| `cannon.wav`           | Successful main-cannon discharge, including visible enemy cannons                      | 0.9 × SFX           |
| `cutting_beam.ogg`     | Actual sustained Courier cutting-beam execution                                        | 0.5 × SFX           |
| `shield.ogg`           | One quiet bed per visible ship/recording while shields operate, with actual-hit spikes | 0.06–0.8 × SFX      |

The cockpit camera retains own-ship combat feedback while hiding its hull. Other combat sources follow the Viewscreen camera frustum and render horizon; changing camera direction does not replay old shots. PDCs, missiles and other beam types receive no new firing recording. Existing explosion and UI-click sounds remain.

Shield audio belongs to the shared shield-effect contributions, including temporary grants. The native hit resolver publishes assigned damage only for reached contributions. The local mixer combines their beds and hit amounts per ship/recording; four arcs do not create four hums. The initial envelope ignores damage below 1, scales normalized intensity from zero at 1 to a full spike at 30, and decays at 1.5 intensity units/second before mapping the bed to peak gain. Cannon sounds follow committed directed launches, and beam loops follow actual sustained directed execution. Both publish through the common cosmetic cue interface; shield audio is owned by the resolved shield effects.

Loops stop on pause/recovery, departure, destruction, leaving the mission or replacing the session. Restoring a session reconstructs current loops without replaying historical shots or shield spikes. Sixteen reusable one-shot voices bound overlapping cannon/explosion playback, replacing the oldest allocation when full. Music, siren and SFX continue to use the existing independent settings and Master gain; no new mixer controls were added.

## Authoring

An entity's optional `audio.ambient` and `audio.engine` each contain an `asset` reference and a numeric `gain` from 0 to 1. Weapon definitions may supply `firing_audio` with the same fields: launch modes require WAV; sustained beam modes require OGG. A shared recording must be compatible with every bound operating mode. These are explicit metadata bindings, not hardcoded weapon-name rules.

A shared shield contribution's optional `audio` object contains `asset`, `gain`, `peak_gain`, `decay_per_second`, `damage_threshold` and `damage_full_spike`. Conditions may additionally set `music_gain` (default 1). Workshop exposes these fields alongside ship fittings, weapon templates, shield contributions and condition music. Asset dependencies use the ordinary scenario/export reference closure.

Referenced audio is decoded by the content verifier without opening an audio device, using the same pinned Vorbis/WAV decoders as raylib. Invalid references, unsupported formats, corrupt recordings and invalid gains/envelopes produce authored-data diagnostics. Playback failures warn and preserve gameplay; a failed condition-music stream falls back to ordinary mission music.

The five V2 MP3s were converted to tracked OGG loops/PCM16 WAV while retaining sample rates and channel counts. `the_final_battle.ogg` was promoted unchanged from V3 raw storage. Sources remain untouched. Each maintained base includes `assets/audio/host-audio-sources.json` with source/output hashes and conversion details, plus the existing provenance in `CREDITS.txt`. Builds require neither V2 nor raw inputs nor conversion tools.

The production pack previously occupied approximately 31.7 MiB; adding the recordings requires a **64 MiB content archive bound**. Loaders, downloads, persistent pack storage, transfers, Workshop workers and pack imports use that bound. The unpacked source-content bound remains 64 MiB, runtime saves retain their separate 128 MiB allowance, and non-pack file imports retain 32 MiB. Recovery accepts up to eight existing 8 MiB chunks per embedded pack.

## Earlier music integration evidence

Authored alert conditions can select a looping mission track and a once-on-entry siren. The ship server plays these once per ship presentation, independent of the number of Console regions. Siren volume is separate from music and interface SFX. Initial snapshots and save recovery do not replay old sirens. See [Bridge Command](bridge-command.md) for authoring and blackboard behaviour.

The ship server plays the menu track while preparing a session and ordinary mission music when no condition supplies a track. The menu/ordinary mission recordings are bundled only for the server role; mission effects are tracked pack assets.

Open **Settings** from the server navigation, or press **Escape** from a server screen (including a local Console). The labelled music slider applies immediately from 0% (mute) to 100%, starting at 50% on first native launch and each browser launch. Native Settings remembers volume and localisation preferences beneath the configured data directory. **Back** or Escape returns to the previous screen without resetting playback or volume. Settings do not pause the simulation. Browser audio starts after the first click/touch allowed by the browser's audio policy.

The menu and ordinary mission recordings loop and switch when the mission starts. Their original downloaded bytes are copied from ignored `raw/audio/` into tracked `content/audio/`; builds do not depend on the raw directory. [Credits and source licences](../content/audio/CREDITS.txt) accompany them in the runtime bundle.

Playback uses raylib streaming in presentation code, with no replicated audio state. An unavailable audio device or failed recording load leaves the game usable without music.

Verification, 18 October 2026: native and all three WASM applications build; all eleven native system checks and the WASM input/semantics check pass. The settings checks cover server-only access, numeric bounds, mute, full volume and return-screen/value retention. Repository formatting checks pass. Manually invoked native home/mission smoke runs initialise WASAPI and load the respective recordings successfully. In Chromium, the slider starts at 50%, accepts 0/100 and arrow-key edits, retains its value on return, and supports mouse Back and Escape. A training-session start unloads the menu stream and loads `/assets/audio/ambient.ogg`; only `server.js` declares the audio files in its preload manifest. These checks establish loading and interaction, rather than a human listening judgement or actual screen-reader usability.
