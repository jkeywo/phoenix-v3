# Selected audition sounds

The six MP3s left in `raw/audio-candidates/` are promoted to PCM16 WAVs in the production and two development bases. The original preview recordings are untouched. `assets/audio/selected-audio-sources.json` records creator, source page, CC0 licence, source/output hashes and conversion details. These are Freesound public MP3 previews, not lossless originals. Builds need neither raw inputs nor conversion tools. Deleted candidates and Kenney packs are unused.

| Candidate                            | Trigger                                                                                                                           | Gain × SFX |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------- | ---------- |
| `pdc_firing`                         | Committed directed interception launches; refused fire stays silent                                                               | 0.3        |
| `ftl_arrival`                        | Successful scenario/interdiction reinforcement spawn with a fitted FTL drive; initial ships and carried craft launches do not cue | 0.45       |
| `ftl_departure`                      | Committed, revalidated FTL departure; one cue despite duplicate input                                                             | 0.45       |
| `comms_attention_transmission_end`   | New delivered conversation messages from another ship; incoming/queued text stays silent                                          | 0.25       |
| `critical_heat_power_damage_warning` | Entering thermal shutdown, at most 25% remaining compartment HP, or one second of continuous power funding shortfall              | 0.2        |
| `psychic_ward_activation`            | Shared psychic ability first produces funded active work, once per activation                                                     | 0.25       |

Comms attention coalesces within one second. Critical warnings require leaving and re-entering the critical state and have a ten-second minimum interval. These thresholds affect presentation only. Comms, warnings and psychic activation remain private to the host ship; PDC and FTL cues follow shared Viewscreen visibility. A departed ship remains an invisible sound source for three seconds so its final cue can finish, while engine/interior loops stop immediately. No new mixer controls, transport sounds or gameplay tuning are added.

Transient receipts and status baselines are not saved. Recovery baselines delivered messages, active abilities and existing critical conditions before advancing work, preventing historical one-shots from replaying. Pause/session replacement continue through the existing mixer reset. Native and browser share the same policy and bounded voice pool.

Verification and listening acceptance are recorded in [the verification receipt](smoke/selected-audio.md).
