# Network observation projection

`projection.cppm` (`phoenix.networking.projection`) owns the existing permitted observation/public-board transport envelope. `gameplay.cpp` implements bounded category prioritisation, omission counts and public-board sanitisation. `observation.cpp` implements observation encoding, verified catalogue permission/roster validation, legacy body compatibility and bounded counters.

The module owns no mutable session or World state. Catalogue definitions, observation contracts and permitted read-model helpers are its data inputs. Compiler calls resolve existing verified Console/system definitions; it acquires no authoritative execution, worker, renderer or OS dependency.

The five public operations preserve their existing `phoenix::network` names. Alert and network-compatible body helpers are private. Read-model helpers are not reexported.

Category ordering, the 1 MiB gameplay retention budget, additional-Console nesting restrictions and legacy body-field handling remain unchanged. Logical read models and exported JS callbacks do not change; the wire owner defines the binary transfer encoding and its 4 MiB bound.

The JSON-shaped objects here are internal permitted read models. `phoenix.wire` encodes live projection bodies as binary CBOR and omits unchanged fields using recipient-local acknowledged deltas; this module still owns canonical projection validation and knowledge filtering. This changes the live body encoding, with matching updated peers required, without changing UI callbacks or authored content formats.

Relevant production-path checks: network-content, shared-display/camera filtering, directed/ability/process/sensor/psychic observations and local/remote continuation.
