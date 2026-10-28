# Environment geometry and fields

Start with `environment.cppm` (`phoenix.scenarios.environment`). It owns region/point/interval declarations, coordinate validation, geometric sampling and deterministic asteroid identities. `definitions.cpp` compiles authored regions against explicitly supplied allowed entity definitions. `fields.cpp` implements exact segment tests, path attenuation, generated cells and permitted display records.

`phoenix_scenarios_environment` depends only on portable foundation data/contracts. It imports no scenario runtime, state trees, content catalogue, World, renderer or private header from another module. Both authoring and physical simulation use the same compiled regions and geometry.

Keep coordinate limits, Q30 interval rounding, map seeds, field identity allocation and integer optical-depth arithmetic unchanged. Region configuration and generated geometry are immutable inputs; the World owns streamed physical instances. Existing scenario, sensors, collision and rendering-data cases use this production path; graphical/performance evidence remains separate.

`streaming.cppm` (`phoenix.scenarios.environment.streaming`) is a separate native module and target. It owns the bounded asteroid window/cell cache and physical field checkpoint codecs/validation, using pure numeric World state contracts. World supplies native focus facts and installs/unloads physical bodies; a prepared window commits only after the existing installation phase completes. This keeps native runtime dependencies out of the portable environment target.
