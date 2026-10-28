# Flight drag feedback

Start with [flight.cppm](flight.cppm).

ThrottleFeedback owns optimistic local previews and reconciles them against admitted model values after release. The surface owns capture and passes a clock explicitly.

Dependencies: ConsoleModel only; no Application, graphics backend or native devices.

Validation: Existing controller/flight cases and explicitly triggered manual-flight UI smoke.
