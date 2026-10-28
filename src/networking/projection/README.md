# Network observation projection

`projection.cppm` (`phoenix.networking.projection`) owns the existing permitted observation/public-board transport envelope. `gameplay.cpp` implements bounded category prioritisation, omission counts and public-board sanitisation. `observation.cpp` implements observation encoding, verified catalogue permission/roster validation, legacy body compatibility and bounded counters.

The module owns no mutable session or World state. Catalogue definitions, observation contracts and permitted read-model helpers are its data inputs. Compiler calls resolve existing verified Console/system definitions; it acquires no authoritative execution, worker, renderer or OS dependency.

The five public operations preserve their existing `phoenix::network` names. Alert and network-compatible body helpers are private. Read-model helpers are not reexported.

Category ordering, the 1 MiB gameplay retention budget, 4 MiB authenticated snapshot bound, additional-Console nesting restrictions and legacy body handling remain unchanged. Browser/wire schemas and exported JS callbacks do not change.

Relevant production-path checks: network-content, shared-display/camera filtering, directed/ability/process/sensor/psychic observations and local/remote continuation.
