# External decision-provider bridge

`inference_bridge.cpp` is the native command-line bridge between the Phoenix inference protocol and an external provider process. Read its pairing/service arguments and bounded transport request handling first. Its dependencies are the inference and wire/transport owners rather than physical World execution or UI.

The bridge preserves provider request/response identity and finite queues; it does not grant a provider hidden state or command authority. Host-side scheduling, permitted observation construction and acceptance remain in their control/session owners.

Existing inference-adapter tests cover protocol validation and failure handling. Actual external-service connectivity is environment-specific evidence. Migration receipts distinguish the compiled executable from a live provider journey.
