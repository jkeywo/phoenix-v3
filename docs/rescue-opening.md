# Rescue under threat: interrupted station delivery

The same mission offers a Courier and Cruiser delivery fit. The briefing and opening station call describe the delivery assignment. Rescue objectives appear only after the distress conversation reaches the crew.

The system contains a procedural star, a single-billboard planet and side-on orbital station, and an asteroid field with a clear southern working corridor. The station stays fixed relative to the planet. Ship fits, cargo, geometry, dialogue and timing are authored in the base pack, without changing the ordinary Courier, Cruiser or shuttle definitions used elsewhere.

## Playing the opening

The Courier carries 100 units of routine station supplies. Helm approaches Cairn, docks at a compatible station port, and Support uses **Stores and deliveries** to transfer the mission hold into the station's **Receiving store**. There is no delivery deadline.

The Cruiser carries 100 units of perishable essentials. Tactical authorises a utility shuttle's cargo use; Engineering orders the delivery in **Stores and deliveries**. One shuttle holds the entire consignment. The Cruiser can divert while the shuttle works, or Helm can dock the Cruiser and Engineering can unload directly. No shuttle launches automatically.

Only physical cargo in Receiving counts. Dispatch, docking, proximity and return flight do not award delivery credit. A partial transfer remains physical progress. Full receipt fixes the grade on that simulation tick:

| Cruiser receipt time from arrival | Delivery grade |
| --- | --- |
| Through 10 minutes | Prompt, full credit |
| After 10 through 20 minutes | Late, reduced credit |
| After 20 minutes | Expired, no delivery credit |

Outstanding Cruiser deliveries receive station reminders at 5, 8, 15 and 18 minutes. Completion is acknowledged; expiry is announced. Queued obsolete calls are cancelled when either terminal result occurs. Expired essentials remain in the physical inventory and can still be transferred.

Crossing the station approach boundary halfway from arrival triggers Morrow's distress call once. Swept player-ship movement detects the crossing even at high speed; carried craft cannot trigger it. The pirate delay begins at that event. Morrow identifies its location, lost propulsion and steering, sealed damaged compartments and injured crew, then clears the existing repair deployment destination. The existing repair choices, optional Medical contribution, recall, recovery and escape remain available.

Departure freezes both the rescue result and a separate delivery assessment. A shuttle still in transit receives no advance delivery credit. The report also records the quantity actually received.

## Authoring and continuation

`ship_briefings` maps selectable ship definitions to briefing text or localisation keys; `briefing_key` provides a reusable fallback. The production catalogue, native menu, browser launcher and lobby resolve the selected ship. Remote crew receive the selected briefing in the lobby projection. Workshop exposes both authoring fields.

Scenario facts support the selected entity definition, active simulation tick, a named store/material quantity, and an objective's completion tick. An objective completion remains latched; report `assessments` evaluate separately from the primary rescue grade. Conversation `cancel_events` suppress both future starts and already queued, undelivered lines. These fields and scenario continuation use the ordinary save and content-identity checks. See [mission objectives](mission-objectives.md) for the generic contracts.

The station approach region is authored at half the initial player-to-station distance. When editing arrival geometry, update that radius with the arrival and station positions. Asteroid generators leave a southern arrival lane and central working space; preserve Cruiser clearance when adjusting terrain.

## Verification

Synthetic production-loader fixtures cover swept approach crossings, exclusion of other entities, receipt and partial unloading, deadline boundaries, expiry, latched completion, independent rescue/delivery grades, and saved continuation. Conversation checks cover cancellation while Comms delivery is delayed. Native/WASM mission, cargo and conversation traces match exactly. Launcher and native/remote lobby checks cover briefing propagation.

The explicitly invoked graphical walkthrough is `phoenix_mission_smoke <pack-directory> delivery-courier` or `delivery-cruiser`. It uses ordinary Helm, Tactical, Engineering, Comms and transport commands and the real Console renderer. Browser builds accept `?mode=delivery-courier` or `?mode=delivery-cruiser` on `phoenix_mission_smoke.html`. Graphical evidence is recorded separately in the smoke receipt; successful automated checks alone do not establish graphical acceptance.
