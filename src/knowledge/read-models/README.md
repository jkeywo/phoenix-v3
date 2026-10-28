# Permitted read models

`phoenix.knowledge.read_models` owns allowlisted encoding, decoding and validation of already-permitted Console/display data. `read_models.cppm` declares the interface; domain implementation files cover contacts, personnel, activities, machinery and reports. Only portable foundation contracts are dependencies.

Native UI, remote host/client adapters and World projection assembly share these operations. They do not query authoritative state or admit commands. The original field bounds, uncertainty, payload limits and refusal behaviour are preserved. This module is separate from authoritative decision snapshot ownership so clients never acquire a runtime dependency just to decode observations. Relevant integration: network projections, input/semantics, personnel, conversations and security.
