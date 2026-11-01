# Granted tactical Console composition

Start with [tactical.cppm](tactical.cppm).

TacticalWidgets owns the separately granted tactical ConsoleModel embedded beside Helm. It assembles controls/target readouts and uses that model for admitted commands.

Dependencies: ConsoleModel, engineering descriptors and localisation. It never reads unrestricted World state.

Validation: Existing controller/session/combat cases and explicitly triggered Tactical UI smoke.

`specialised.cpp` owns weapons/missiles, selected-target readouts and target actions. Rendering borrows the already-permitted primary/granted models and catalogue, returning shared markup/readout/action records. It preserves existing source order, Main/Details placement and control identities; the surface still mounts and routes them.
