# Interrupted station delivery — 23 October 2026

The authored increment is described in [Rescue opening](../rescue-opening.md). The content was verified as `phoenix-base` revision `46e3278d126dff024e8582a7281f6c122f0cf0c26397a7e9d6b2bc2a33f8e6db`: 40 entity definitions, one mission and 20 Consoles. The planet and side-on starbase are generated transparent PNG sources, packaged as single billboards; their [provenance](../../content/base/assets/rescue/README.md) is recorded separately.

## Automated evidence

- Native and WASM mission, cargo and conversation cases passed. Synthetic production-loader fixtures exercise swept approach crossings, exclusion of other hulls, physical and partial receipt, both inclusive deadlines, irreversible expiry, latched completion, independent rescue/delivery assessments and saved continuation without duplicate credit or messages.
- Native/WASM continuation traces matched exactly after normalising the terminal newline. Files are `build/rescue-{missions,cargo,conversation}-{native,wasm}.trace`; the extended cargo outage trace is `build/rescue-cargo-wasm-final.trace`.
- The extended cargo case rejects a second order for a committed shuttle, interrupts radio communications during outbound flight, saves/resumes during the outage, then restores communications and verifies identical delivery/recovery state.
- All 27 launcher tests passed (`build/rescue-launcher-compact-tests.log`). The real local transport lobby case passed selected-ship changes and matching host/remote briefings (`build/rescue-lobby-final.log`).
- Native game and WASM client/server/Workshop builds passed (`build/rescue-native-final2.log`, `build/rescue-web-final2.log`). Isolated build directories avoid mixing generated files with other concurrent work.

## Manually triggered graphical evidence

The walkthroughs use the real local Session, ordinary owned commands, production content and Console renderer. They render a 2560 × 1080 window with two 1280 × 1080 console regions, plus the full-width Viewscreen. They are explicitly invoked smoke journeys, not mandatory production-content tests or balance assertions.

The native Courier journey passed docking, physical receipt of all 100 supplies, distress and destination clearance, deployment, completed repairs, recall, party recovery, casualty escape and FTL departure. Its report shows **Rescue successful** and the separate **Supplies delivered** assessment. See `build/rescue-courier-final.log` and `build/rescue-courier-report.png`.

The Cruiser walkthrough exposed a mission fitting gap: its inherited 24-part repair kits ran dry during detached structural work, even though the parent still held bulk spares. Deployed kits correctly cannot refill from the parent. The delivery fit now provisions finite 2,000-part kits; the ordinary Cruiser and unrelated scenarios retain their existing inventory. The corrected full run is `build/rescue-cruiser-provisioned.log`.

That full Cruiser run passed Tactical shuttle authorisation, Engineering cargo dispatch, a radio interruption during outbound flight, parent-ship diversion, restored communications, physical receipt, repair deployment, completed repairs, party recovery, casualty escape, shuttle recovery and departure. The inspected `build/rescue-cruiser-report.png` shows **Rescue successful** and **Prompt delivery — full credit**. The frozen report records all 100 essentials received, no personnel lost or left deployed, and the repair party recovered.

Native captures inspected include `build/rescue-courier-arrival-scene.png`, `build/rescue-cruiser-star-scene.png`, `build/rescue-courier-received.png` and `build/rescue-courier-distress.png`. These show the side-on station and planet, procedural star, ordinary cargo panel and authored distress/clearance dialogue. Full cargo quantities and personnel details remain in the scrollable report and its semantic readout.

The browser launcher was inspected through its ordinary Host Game → New Game path. Ship selection displays only the choices. The selected ship's compact briefing appears in the lobby alongside its stations; the Cruiser text retains both deadlines and Tactical/Engineering shuttle responsibilities. Native lobby captures at 1280 × 720, 1100 × 720 and 800 × 900 show the full compact briefing without introducing a scrollbar (`build/rescue-lobby-*.png`, `build/rescue-lobby-compact.log`).

The browser production-loader export accepted the opening pack before the final repair-kit adjustment (`build/rescue-browser-production-verify.log`). An older test origin contained saved intermediate draft content; the fresh loopback origin loaded cleanly. No saved user content was deleted to obtain that result.

The browser `preview-cruiser` journey passed. Its rendered `cruiser-arrival-scene` and `cruiser-star-scene` captures were inspected: planet and side-on station billboards, asteroid scenery, procedural star and delivery objectives were visible. The capture helper now reads the canvas before yielding, avoiding a discarded WebGL buffer. This is browser scenery/lobby evidence, not a complete browser rescue walkthrough.

The walkthrough driver waits for crew movement to transport and the repair party's completion conversation before recalling them. Restoring propulsion alone does not mean the whole repair operation has finished. Native runs save a real `build/rescue-{courier,cruiser}-arrival.zip` checkpoint before deployment; an optional third argument resumes that archive with fresh Console claims. No mission state or cargo is manufactured for these journeys.

Final walkthrough targets compiled on both platforms (`build/rescue-native-smoke-final.log`, `build/rescue-web-capture-final.log`). Earlier unsuccessful driver runs and the repair-kit stall are diagnostic attempts, not acceptance evidence; the passing logs identified above establish the completed journeys.

Large captures and logs remain in the local build directory, outside source-controlled fixtures. This pass does not establish six-display frame rates, physical touch/gamepad operation, screen-reader operation, or combat balance.
