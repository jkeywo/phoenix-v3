# Flight widgets and drag feedback

Start with `flight.cppm`. `ThrottleFeedback` owns optimistic local previews and reconciles them against admitted values after release. The surface owns device capture and supplies the clock.

`specialised.cpp` owns the restored flight-action, assisted/manual status and lateral-button presentation. Its inputs are a permitted ConsoleModel, route prefix and narrow-layout fact. It returns markup/readouts/actions through shared document presentation records; it neither retains Application nor acquires command authority. The surface mounts the result and retains held-control routing.

Dependencies include ConsoleModel, shared document output, engineering descriptor helpers, localisation and rendering helpers. No mutable session or native devices enter the interface.

Validation uses existing input/flight/Controller integrations and explicitly invoked real-Console journeys; see `docs/module-ownership-followup.md`.
