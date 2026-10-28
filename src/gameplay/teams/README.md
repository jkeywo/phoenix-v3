# Compartment routes and remaining team integration

`routes.cppm` exports `phoenix.gameplay.teams.routes`; link `phoenix_gameplay_team_routes`. compartment_route is the generic bounded shortest-route operation over caller-supplied links. It returns the first hop/cost and optional total travel cost without reading a registry, catalogue or hidden World state.

Native team/access integration now compiles in `src/world/bindings/gameplay_teams_01.cpp`, `gameplay_teams_02.cpp` and `gameplay_compartment_access.cpp`, replacing the former included headers. Team task ownership, access and progress must not be declared extracted merely because routing has a named owner. Combat/personnel definitions and World composition decide which routes and tasks are permitted.

Existing repair, boarding, personnel and team journeys cover route ties, blocked paths and resumed work. Preserve authored link order and integer travel cost. Central evidence records the runtime/continuation checkpoints; module compilation alone does not verify a complete team journey.
