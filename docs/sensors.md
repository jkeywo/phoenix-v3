# Sensors and focused scanning

Radar fittings provide contact detection plus independently authored capabilities. `shows` still filters contact types; it does not grant identity or internal information.

```json
{
  "kind": "radar",
  "range_mm": 350000,
  "arc_mdeg": 15000,
  "bearing_mdeg": 0,
  "turret": "sensor-turret",
  "shows": ["ship"],
  "capabilities": {
    "shield_sectors": 200,
    "compartment_condition": 400,
    "findings": 0
  }
}
```

Capabilities are `basic_identity`, `own_arcs`, `contact_arcs`, `shield_sectors`, `compartment_condition` and `findings`. Values are acquisition ticks (20 ticks/second). Omitted capability maps retain compatibility identity and own-arc sensing only. An explicitly empty map detects anonymous contacts. Identity uses the target's localized `class_key` and observed faction disposition, never its private entity definition.

The ordinary `sensor_target` Controller channel selects one target per scanner; zero clears it. Weapon channels remain independent. Each scanner retains its own exposure per target and capability. A selected target acquires all supported capabilities concurrently. Full operating power, an operational mount, range and actual beam coverage are required for acquisition. Broad contact detection retains the existing reduced-range behavior under partial power. Nebula attenuation and sensor-range officer bonuses still apply.

Turrets use existing traversal, structural damage and rendering. A scanner turret has one targeting owner and cannot also aim a weapon. Beam coverage uses the mount's physical location and actual heading. Lost targets are aimed at their last permitted position. Changing targets or losing qualification retains unfinished exposure; acquired snapshots retain their observation ticks. Returning to qualifying scanning updates completed findings live.

Weapon and scanner aiming reuse the same permitted contact cache per Console during each preparation pass. The cache is discarded after the pass; it cannot carry contacts across ticks or Console boundaries. Scanner aiming supplies position and bearing directly to the existing turret geometry calculation.

Tactical's **Inspect contact**, **Scan target** and **Clear scan target** actions are available through the same semantic controls used by keyboard and screen readers. Readouts show acquisition percentages, blockers, identity and live or last-observed findings. Own and selected-contact weapon arcs have separate toggles. The narrow beam uses the physical scanner heading; sensor mounts are excluded from weapon arcs.

## Scenario findings

Maps can declare localized findings:

```json
{
  "sensor_findings": [
    {
      "id": "isolated-drive",
      "target_entity": 1,
      "text_key": "Engineering scan finding",
      "requires": ["findings", "compartment_condition"],
      "scan_ticks": 300,
      "requires_event": "casualty-available",
      "unless_event": "casualty-departed",
      "discovery_event": "drive-discovered"
    }
  ]
}
```

Event fields are optional and reference declared scenario events. Availability is checked before accumulating exposure. `requires` identifies capabilities the scanner must possess, not previously completed findings. The greater of `scan_ticks` and the fitting's `findings` acquisition time applies. Discovery emits once per observing ship, target and finding. The rescue casualty includes an engineering finding without changing the Comms/Dispatch mission flow. Workshop exposes scanner capability maps, mount fields and map findings.

Findings enter the existing knowledge system as `phoenix.sensor.<capability>` records, with the fitted scanner as provenance. Console equipment feeds determine direct access; authored knowledge feeds and deliberate publications can share the `payload` field. Detailed observations are never automatically shipwide. Network projections use an explicit finding-field allowlist, excluding equipment faults, ammunition and weapon readiness.

Radar identity and detailed readouts use one permitted evidence resolver. It selects the newest observation across fitted scanner snapshots and received knowledge records, preferring a live direct observation on equal timestamps. Instantaneous capabilities use the same power and coverage qualification as timed acquisition, including at world initialisation without advancing acquisition time. Partial power can detect an anonymous contact without acquiring its identity. A received identity can identify a detected contact without granting scanner control.

Per-scanner snapshots retain independent acquisition and physical aiming state. Knowledge records retain the scoped copies used by authored feeds and deliberate publications. Changed findings publish immediately; unchanged findings publish every 20 ticks. Direct snapshots retain their per-tick observation timestamps, and received copies retain the timestamps of the evidence actually published. Shared evidence is shown as last observed rather than a direct live return.

Saved sensor state contains selected targets, independent partial exposure, last observed positions, findings and timestamps. Saves otherwise compatible with the current Controller save format but lacking sensor state start with empty scans. Sensor discovery deduplication also survives restoration.

## Verification

`phoenix_sensors_tests` uses synthetic combat fittings through production loaders and Controller admission. `--trace` supports native/WASM continuation comparison. `phoenix_sensors_smoke` is an explicitly invoked graphical Courier journey, excluded from automatic tests. Build it with `scripts/build.ps1 -Target native -BuildTarget phoenix_sensors_smoke`, then run it with `build/native` as the pack directory. The equivalent WASM target produces an HTML harness.

The sensor simplification follow-up was checked in an isolated source snapshot to exclude concurrent FTL edits. Native build/content checks and all 27 system integration cases passed. The five related WASM cases (sensors, combat, radar/simulation, Controllers and knowledge) passed; sensor, combat, simulation and Controller continuation traces matched native. Changed C++ and documentation files passed their formatting checks. The sensor case additionally verifies anonymous detection under partial power and consistent identity after deliberate sharing.

The first service-gate run overlapped WASM linking and reported two admission timeouts plus a temporary-directory cleanup failure. A repeat after linking completed passed all eight service cases, including fleet recovery, live sessions, verified WSS and pre-game admission.

Verification on 21 October 2026:

- `scripts/check.ps1 -Target native` passed all 27 system cases, content/build checks and service checks.
- The sensor continuation case saves separately during turret traversal and genuinely unfinished acquisition, then compares restored and uninterrupted state.
- The final native and WASM sensor continuation traces match.
- The broader WASM system gate and combat, Controller, Comms, sensor and away-mission continuation comparisons passed. The final finding-deduplication correction is additionally covered by the focused sensor and graphical journeys.
- The native graphical Courier journey passed select, slew, pause, resume, acquire and retained-observation checks. Captures were inspected at 1280 × 900; the contact panel's nested widths were corrected after visual inspection.
- The same Courier journey passed in the in-app browser with WebGL rendering. The acquired panel and exposed semantic controls were inspected, including timestamped retained findings and the rescue engineering discovery.
- Compatibility capabilities on the legacy Courier and combat-radar template passed production pack verification. Courier timing and ranges remain authored tuning, not exact-value test expectations.

The browser harness at `build/web/phoenix_sensors_smoke.html` displays its result and four captured stages. Physical multi-display and assistive-reader acceptance remain separate from these semantic-control and graphical checks.

Enquiry queues, intrusive scans, spoofing, triangulation, probes and individual equipment fault diagnosis remain outside this increment.
