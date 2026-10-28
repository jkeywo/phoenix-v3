# Flight definition ownership

`flight.cppm` (`phoenix.flight`) and `flight_definitions.cpp` belong to `phoenix_gameplay_flight_definitions`, registered by `definitions.cmake`. They depend only on foundation data and compile immutable native propulsion/steering parameters plus modifier bindings.

This definition target is separate from the authoritative `phoenix_gameplay_flight` runtime target and its `runtime.cppm` interface. Authoring and verification can compile a fit without linking physical flight execution. Keep authored units, bounds, modifier ordering and definition signatures unchanged; existing paid-flight and content cases exercise the production compiler.
