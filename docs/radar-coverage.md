# Courier weapons and radar coverage

The Courier carries two gun turrets with full 360-degree traverse, one beam laser, two independently controlled torpedo tubes and a defensive PDC. The extra forward gun is removed. Both tubes use the existing shared homing/Havok magazines, with separate enablement, ammunition selection, loading and firing state. The salvo action fires eligible loaded tubes once. Lower-complexity ammunition assistance remains in place.

Helm shows weapon coverage rather than overlapping mount limits and aiming tolerances. Equivalent coverage is grouped into one labelled outline with a count and named members. Beam coverage uses the beam mount's existing traverse; PDC coverage uses its existing mount and appears while enabled with an active targeting policy. Homing-selected tubes have no coverage overlay; Havok-selected tubes do. Selection changes the overlay immediately, including during unloading/loading; loaded-ammunition readouts continue to describe the actual physical round.

Helm hides directional sensor sectors. Sensor operation, sensing evidence and contact arc findings retain their existing rules. These are presentation settings, independent of physical aiming, launch geometry and target eligibility.

## Authoring contract

| Owner                       | Field                   | Meaning and default                                                                                                                                                                                |
| --------------------------- | ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Weapon equipment            | `radar_coverage`        | `legacy` (default), `coverage`, or `hidden`. Coverage emits one entry for the weapon, using its turret mount's traverse and rest bearing when fitted. Legacy retains the former mount/aim display. |
| Weapon equipment            | `coverage_label`        | Localisation key for the coverage label; defaults to equipment identity. Also names coverage-configured weapons in the compact weapon panel.                                                       |
| Weapon equipment            | `coverage_when_enabled` | Default false. When true, hide coverage while equipment is disabled or its targeting policy is Off. Structural unavailability is otherwise shown as disabled coverage.                             |
| Resource ammunition profile | `radar_coverage`        | Optional boolean overriding whether coverage-configured weapons using the selected magazine/resource are displayed. Omission inherits weapon visibility; a hidden weapon remains hidden.           |
| Sensor equipment            | `radar_arc`             | Default true. Controls whether the sensor's directional sector may be drawn.                                                                                                                       |
| Console radar widget        | `fire_arcs`             | Existing switch enabling weapon overlays, combined with the player's Fire arcs toggle.                                                                                                             |
| Console radar widget        | `weapon_arcs`           | Optional array of fitted weapon identities to display. Omission includes all permitted entries; an empty array includes none.                                                                      |
| Console radar widget        | `sensor_arcs`           | Default true. Allows directional sensor sectors on this radar; each sensor must also permit its sector.                                                                                            |

Workshop exposes these settings alongside weapon, ammunition, sensor and radar authoring. Content compilation validates their types and supported values. Console feed and own-arc sensing permissions remain prerequisites: display preferences cannot expose unavailable equipment observations.

Visual and accessible descriptions use the same filtering/grouping helper. Group membership names every displayed weapon; differing operational states remain separate. Full circles have no radial boundary lines. Beam outlines are heavier; defensive outlines are dashed, so identification does not depend on colour alone.

Existing content omitting the new settings keeps legacy visibility. Existing saves embed their stored fitting; create a new session for the corrected Courier fit. Mission instances and their explicit console overrides have been updated without changing their mission circumstances.

## Verification

Synthetic combat contracts exercise full-traverse firing at rotated headings, shared-magazine tube loading, ammunition-dependent coverage during transitions, defensive enablement, console feed restrictions, grouped member identity and continuation. Invalid weapon, ammunition and console settings are refused through production loaders.

Executed build, system-check and graphical evidence is recorded in [the smoke receipt](smoke/courier-radar-coverage.md).
