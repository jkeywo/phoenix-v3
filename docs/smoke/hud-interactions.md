# Bridge HUD interaction checks — 19 October 2026

Native validation used a fixed snapshot of `c5550f1` plus the HUD changes, built in `build/hud-native`. This avoided concurrent Controller-refactor source edits and shared build-directory locks. The browser client also compiled successfully with the shared presentation changes.

- `phoenix_menu_tests`: passed, including five-degree directional hysteresis, reverse boundary crossing, wraparound and large-turn selection.
- `phoenix_system_tests`: passed input routing and semantic admission checks.
- `phoenix_session_tests`: passed, including rejecting a native player Settings quit action while retaining its claimed station.
- Offline Courier Engineering (`--engineering-smoke`): passed live heat/battery telemetry, manual takeover, admitted reactor output/power/coolant changes and subsequent telemetry confirmation. The probe checks positive card-container geometry. The inspected final capture is `build/engineering-final.png`.
- Offline Courier Helm (`--helm-smoke`): passed pointer throttle/heading, independent native Settings, Away/Resume and advancing simulation ticks. Capture: `build/helm-final.png`. This is an interaction check, not a latency benchmark.
- Virtual native multi-screen lobby (`--lobby-regions-smoke`): passed launch, routed Helm interaction, simultaneous host/player Settings, denied player release/quit, host save and host quit. The inspected simultaneous-overlay capture is `build/menu-native-game-settings.png`.
- The inspected Viewscreen capture (`build/viewscreen-final.png`) shows the matching cog and thin frame. The new PNG retains its generated alpha; sampled central pixels are fully transparent.

The native journeys used isolated profile folders under `build/`. They do not establish physical multi-monitor, screen-reader or touch-device acceptance. These smoke runs remain manually invoked; no production-content goldens or performance thresholds were added.
