# Flight definitions, integration and native actuation

Read `flight.cppm` (`phoenix.flight`, `phoenix_gameplay_flight_definitions`) for immutable propulsion/steering parameters and authored modifier bindings. `runtime.cppm` exports `phoenix.gameplay.flight` through `phoenix_gameplay_flight`: flight commands, guidance, impulse state, demand, fixed integration and continuation. `basic.cpp` contains the basic engine/steering steps; `flight.cpp` owns the richer runtime.

`process_work.cppm` (`phoenix.gameplay.flight.work`) owns funded work quotes and BasicFlightWorkBook, which preserves shared-body steering-before-engine candidate order. `native_actuators.cppm` (`phoenix.gameplay.flight.native`) is the newly registered native service: it owns four equipment queries and actor-scoped property/contact/telemetry ports. It receives ProcessMachineryBook per synchronous phase call rather than retaining a book reference.

The native service is adopted through actor-scoped ports and query ownership. World still chooses phase order and composes passive non-flight requests in its lifecycle binding units. Navigation assessment now compiles in `src/world/travel-bindings/gameplay_navigation_assessment.cpp`; the former included header is no longer an active implementation boundary. Independent provider compilation and actual World continuation remain separate validation receipts.

Existing paid-flight, guidance, resource, process and Controller cases protect fixed arithmetic, preview restoration, charge/heat fractions and continuation. Provider compilation is only the first gate; consult the migration evidence for actual World adoption and runtime/trace results.
